// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2013-2016 AURA/LSST.
 *
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the LSST License Statement and
 * the GNU General Public License along with this program.  If not,
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */

/**
  * @file
  *
  * @brief PostPlugin does the right thing to handle LIMIT (and
  * perhaps ORDER BY and GROUP BY) clauses.
  *
  * @author Daniel L. Wang, SLAC
  */

// Class header
#include "qana/PostPlugin.h"

// System headers
#include <cstddef>
#include <stdexcept>
#include <string>

// Third-party headers

// LSST headers
#include "lsst/log/Log.h"

// Qserv headers
#include "global/constants.h"
#include "global/stringUtil.h"
#include "qana/AnalysisError.h"
#include "query/FuncExpr.h"
#include "query/OrderByClause.h"
#include "query/QueryContext.h"
#include "query/SelectList.h"
#include "query/SelectStmt.h"
#include "query/ValueFactor.h"
#include "util/IterableFormatter.h"

namespace {
LOG_LOGGER _log = LOG_GET("lsst.qserv.qana.PostPlugin");
}

namespace lsst {
namespace qserv {
namespace qana {

////////////////////////////////////////////////////////////////////////
// PostPlugin implementation
////////////////////////////////////////////////////////////////////////
void
PostPlugin::applyLogical(query::SelectStmt& stmt,
                         query::QueryContext& context) {
    _limit = stmt.getLimit();
    if (stmt.hasOrderBy()) {
        _orderBy = stmt.getOrderBy().clone();
    }
}

void
PostPlugin::applyPhysical(QueryPlugin::Plan& plan,
                          query::QueryContext& context) {
    // Idea: If a limit is available in the user query, compose a
    // merge statement (if one is not available)
    LOGS(_log, LOG_LVL_DEBUG, "Apply physical");

    if (_limit != NOTSET) {
        // [ORDER BY ...] LIMIT ... is a special case which require sort on worker and sort/aggregation on czar
        if (context.hasChunks()) {
             LOGS(_log, LOG_LVL_DEBUG, "Add merge operation");
             context.needsMerge = true;
         }
    } else if (_orderBy) {
        // If there is no LIMIT clause, remove ORDER BY clause from all Czar queries because it is performed by
        // mysql-proxy (mysql doesn't garantee result order for non ORDER BY queries)
        LOGS(_log, LOG_LVL_TRACE, "Remove ORDER BY from parallel and merge queries: \""
             << *_orderBy << "\"");
        for (auto i = plan.stmtParallel.begin(), e = plan.stmtParallel.end(); i != e; ++i) {
            (**i).setOrderBy(nullptr);
        }
        if (context.needsMerge) {
            plan.stmtMerge.setOrderBy(nullptr);
        }
    }

    // For query results to be ordered, the columns and/or aliases used by the ORDER BY statement must also
    // be present in the SELECT statement. Only unqualified column names in the SELECT statement and that are
    // *not* in a function or expression may be used by the ORDER BY statement. For example, things like
    // ABS(col), t.col,  and col * 5 must be aliased if they will be used by the ORDER BY statement.
    //
    // This block creates a list of column names & aliases in the select list that may be used by the
    // ORDER BY statement.
    if (_orderBy) {

        auto validSelectCols = getValidOrderByColumns(plan.stmtOriginal);
        auto usedSelectCols = getUsedOrderByColumns(plan.stmtOriginal);

        // For each element in the ORDER BY clause, see if it matches one item in validSelectCol
        auto orderBy = plan.stmtOriginal.getOrderBy();
        auto ordByTerms = orderBy.getTerms();
        LOGS(_log, LOG_LVL_DEBUG, "ORDER BY terms:" << util::printable(*ordByTerms));
        for (auto const& ordByTerm : *ordByTerms) {
            auto const& expr = ordByTerm.getExpr();
            query::ColumnRef::Vector orderByColumnRefVec;
            expr->findColumnRefs(orderByColumnRefVec);
            for (auto const & ordByColRef : orderByColumnRefVec) {
                // Check if the ORDER BY column matches a single valid column
                query::ColumnRef::Ptr match;
                for (auto const & selCol : validSelectCols) {
                    if (*selCol == *ordByColRef) {
                        if (match != nullptr) {
                            throw AnalysisError("ORDER BY Duplicate match for " + toString(ordByColRef)
                                    + " in SELECT columns:" + toString(util::printable(validSelectCols)));
                        }
                        match = selCol;
                    }
                }
                // Check that there is a match for ORDER BY column
                if (nullptr == match) {
                    throw AnalysisError("ORDER BY No match for " + toString(ordByColRef) + " in SELECT columns:"
                            + toString(util::printable(validSelectCols)));
                }
            }
        }
    }

    if (context.needsMerge) {
        // Prepare merge statement.
        // If empty select in merger, create one with *
        query::SelectList& mList = plan.stmtMerge.getSelectList();
        auto vlist = mList.getValueExprList();
        if (not vlist) {
            throw std::logic_error("Unexpected NULL ValueExpr in SelectList");
        }
        // FIXME: is it really useful to add star if select clause is empty?
        if (vlist->empty()) {
            mList.addStar(std::string());
        }
    }
}


query::ColumnRef::Vector PostPlugin::getValidOrderByColumns(query::SelectStmt const & selectStatement) {

    std::shared_ptr<query::ValueExprPtrVector> selectValueExprList =
            selectStatement.getSelectList().getValueExprList();

    query::ColumnRef::Vector validSelectCols;
    LOGS(_log, LOG_LVL_DEBUG, "finding columns usable by ORDER BY from SELECT valueExprs:"
            << util::printable(*selectValueExprList));
    for (auto const& selValExpr : *selectValueExprList) {
        std::string alias = selValExpr->getAlias();
        // If the SELECT column has an alias, the ORDER BY statement must use the alias.
        if (!alias.empty()) {
            validSelectCols.push_back(std::make_shared<query::ColumnRef>("", "", alias));
        } else {
            // if the ValueExpr is not of type COLUMN, getColumnRef will return nullptr;
            auto const & selColumnRef = selValExpr->getColumnRef();
            if (nullptr == selColumnRef) {
                continue;
            }
            validSelectCols.push_back(selColumnRef);
        }
    }
    LOGS(_log, LOG_LVL_DEBUG, "valid colNames=" << util::printable(validSelectCols));
    return validSelectCols;
}


query::ColumnRef::Vector PostPlugin::getUsedOrderByColumns(query::SelectStmt const & selectStatement) {
    // For each element in the ORDER BY clause, see if it matches one item in validSelectCol
    query::ColumnRef::Vector usedColumns;
    auto ordByTerms = selectStatement.getOrderBy().getTerms();
    for (auto const& ordByTerm : *ordByTerms) {
        auto const& expr = ordByTerm.getExpr();
        query::ColumnRef::Vector orderByColumnRefVec;
        expr->findColumnRefs(orderByColumnRefVec);
        usedColumns.insert(usedColumns.end(), orderByColumnRefVec.begin(), orderByColumnRefVec.end());
    }
    return usedColumns;
}

}}} // namespace lsst::qserv::qana
