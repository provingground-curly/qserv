// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2015-2016 AURA/LSST.
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

// System headers
#include <array>
#include <memory>
#include <string>
#include <unistd.h>

// Boost unit test header
#define BOOST_TEST_MODULE CControl_1
#include "boost/test/included/unit_test.hpp"

#include <boost/test/included/unit_test.hpp>
#include <boost/test/data/test_case.hpp>

// Qserv headers
#include "ccontrol/UserQueryType.h"
#include "parser/ParseException.h"
#include "parser/SelectParser.h"
#include "qproc/QuerySession.h"
#include "query/AndTerm.h"
#include "query/BetweenPredicate.h"
#include "query/BoolFactor.h"
#include "query/BoolTerm.h"
#include "query/CompPredicate.h"
#include "query/InPredicate.h"
#include "query/LikePredicate.h"
#include "query/OrTerm.h"
#include "query/SelectList.h"
#include "query/SelectStmt.h"
#include "query/ValueFactor.h"
#include "query/WhereClause.h"

namespace test = boost::test_tools;
using namespace lsst::qserv;

BOOST_AUTO_TEST_SUITE(Suite)


// in many cases we can use the antlr2-parser-generated IR to test the IR generated by the antlr4 parser.
// This requires that the query can be parsed by our antlr2 parser code, and as we add query coverage
// to our antlr4 parser code we will not be able to use the antlr2 IR for testing.
// Instead, we test these queries purely with the antlr4 parser, by generating a similar statement and we
// then manually change that IR to be like the expected IR, and compare that with the IR generated by the
// test statement.
// We know the changed test statement IR is correct because we have a test that compares it with the
// antlr2-generated statement, in antlr_compare.

struct Antlr4CompareQueries {
    Antlr4CompareQueries(std::string const & iQuery, std::string const & iCompQuery,
            std::function<void(query::SelectStmt::Ptr const&)> const & iModFunc,
            std::string const & iSerializedQuery=std::string())
        : query(iQuery)
        , compQuery(iCompQuery)
        , serializedQuery(iSerializedQuery)
        , modFunc(iModFunc)
    {}


    // query to test, that will be turned into a SelectStmt by the andlr4-based parser.
    std::string query;

    // comparison query, that will be turned into a SelectStmt by the andlr4-based parser and then that will
    // be modified by modFunc
    std::string compQuery;

    // the expected query string to be generated by generating sql from the SelectStmt generated for `query`.
    std::string serializedQuery;

    // modFunc is a function that will modify the SelectStmt generated for `compQuery`, to match the expected
    // SelectStmt generated for `query`.
    std::function<void(query::SelectStmt::Ptr const)> modFunc;
};

std::ostream& operator<<(std::ostream& os, Antlr4CompareQueries const& i) {
    os << "Antlr4CompareQueries(" << i.query << "...)";
    return os;
}

static const std::vector<Antlr4CompareQueries> ANTLR4_COMPARE_QUERIES = {
    // tests NOT LIKE (which is 'NOT LIKE', different than 'NOT' and 'LIKE' operators separately)
    Antlr4CompareQueries(
        "SELECT shortName FROM Filter WHERE shortName NOT LIKE 'Z'",
        "select shortName from Filter where shortName LIKE 'Z'",
        [](query::SelectStmt::Ptr const & selectStatement) {
            // flip the 'not' on the 'without' statement to make it 'with not'
            auto whereClauseRef = selectStatement->getWhereClause();
            auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(whereClauseRef.getRootTerm());
            auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
            auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
            auto likePredicate = std::dynamic_pointer_cast<query::LikePredicate>(boolFactor->_terms[0]);
            likePredicate->hasNot = true;
        },
        "SELECT shortName FROM Filter WHERE shortName NOT LIKE 'Z'"
    ),

    // tests quoted IDs
    Antlr4CompareQueries(
        "SELECT `Source`.`sourceId`, `Source`.`objectId` From Source WHERE `Source`.`objectId` IN (386942193651348) ORDER BY `Source`.`sourceId`",
        "SELECT Source.sourceId, Source.objectId From Source WHERE Source.objectId IN (386942193651348) ORDER BY Source.sourceId",
        nullptr,
        "SELECT Source.sourceId,Source.objectId FROM Source WHERE Source.objectId IN(386942193651348) ORDER BY Source.sourceId"
    ),

    // tests the null-safe equals operator
    Antlr4CompareQueries(
        "SELECT ra_PS FROM Object WHERE objectId<=>417857368235490",
        "SELECT ra_PS FROM Object WHERE objectId = 417857368235490",
        [](query::SelectStmt::Ptr const & selectStatement) {
            // change the equals op to be the null safe equals op
            auto whereClauseRef = selectStatement->getWhereClause();
            auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(whereClauseRef.getRootTerm());
            auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
            auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
            auto compPredicate = std::dynamic_pointer_cast<query::CompPredicate>(boolFactor->_terms[0]);
            compPredicate->op = query::CompPredicate::NULL_SAFE_EQUALS_OP;
        }
    ),

    // tests the NOT BETWEEN operator
    Antlr4CompareQueries(
        "SELECT objectId,ra_PS FROM Object WHERE objectId NOT BETWEEN 417857368235490 AND 420949744686724",
        "SELECT objectId, ra_PS FROM Object WHERE objectId BETWEEN 417857368235490 AND 420949744686724;",
        [](query::SelectStmt::Ptr const & selectStatement) {
            // change the BetweenPredicate's hasNot to true
            auto whereClauseRef = selectStatement->getWhereClause();
            auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(whereClauseRef.getRootTerm());
            auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
            auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
            auto betweenPredicate = std::dynamic_pointer_cast<query::BetweenPredicate>(boolFactor->_terms[0]);
            betweenPredicate->hasNot = true;
        }
    ),

    // tests the && operator.
    // The Qserv IR converts && to AND as a result of the IR structure and how it serializes it to string.
    Antlr4CompareQueries(
        "select objectId, iRadius_SG, ra_PS, decl_PS from Object where iRadius_SG > .5 && ra_PS < 2 && decl_PS < 3;", // &&
        "select objectId, iRadius_SG, ra_PS, decl_PS from Object where iRadius_SG > .5 AND ra_PS < 2 AND decl_PS < 3;", // AND
        nullptr,
        "SELECT objectId,iRadius_SG,ra_PS,decl_PS FROM Object WHERE iRadius_SG>.5 AND ra_PS<2 AND decl_PS<3"
    ),

    // tests the || operator.
    // The Qserv IR converts || to OR as a result of the IR structure and how it serializes it to string.
    Antlr4CompareQueries(
        // The numbers used here; 400000000000000 and 430000000000000 are arbitrary, but do represent values
        // that one may see for objectId in the lsst qserv database and so are reasonable choices for a unit
        // test.
        "select objectId from Object where objectId < 400000000000000 || objectId > 430000000000000 ORDER BY objectId;", // ||
        "select objectId from Object where objectId < 400000000000000 OR objectId > 430000000000000 ORDER BY objectId", // OR
        nullptr,
        "SELECT objectId FROM Object WHERE objectId<400000000000000 OR objectId>430000000000000 ORDER BY objectId"
    ),

    // tests NOT IN in the InPredicate
    Antlr4CompareQueries(
        "SELECT objectId, ra_PS FROM Object WHERE objectId NOT IN (417857368235490, 420949744686724, 420954039650823);",
        "SELECT objectId, ra_PS FROM Object WHERE objectId IN (417857368235490, 420949744686724, 420954039650823);",
        [](query::SelectStmt::Ptr const & selectStatement) {
            // change the BetweenPredicate's hasNot to true
            auto whereClauseRef = selectStatement->getWhereClause();
            auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(whereClauseRef.getRootTerm());
            auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
            auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
            auto inPredicate = std::dynamic_pointer_cast<query::InPredicate>(boolFactor->_terms[0]);
            inPredicate->hasNot = true;
        },
        "SELECT objectId,ra_PS FROM Object WHERE objectId NOT IN(417857368235490,420949744686724,420954039650823)"
    ),

    // tests the modulo operator
    Antlr4CompareQueries(
        "select objectId, ra_PS % 3, decl_PS from Object where ra_PS % 3 > 1.5",
        "select objectId, ra_PS % 3, decl_PS from Object where ra_PS - 3 > 1.5",
        [](query::SelectStmt::Ptr const & selectStatement) {
            // change the subtraction value expr to modulo:
            auto whereClauseRef = selectStatement->getWhereClause();
            auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(whereClauseRef.getRootTerm());
            auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
            auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
            auto compPredicate = std::dynamic_pointer_cast<query::CompPredicate>(boolFactor->_terms[0]);
            query::ValueExpr::FactorOp& factorOp = compPredicate->left->getFactorOpsRef()[0];
            factorOp.op = query::ValueExpr::MODULO;
        },
        "SELECT objectId,(ra_PS % 3),decl_PS FROM Object WHERE (ra_PS % 3)>1.5"
    ),

    // tests the MOD operator
    Antlr4CompareQueries(
        "select objectId, ra_PS MOD 3, decl_PS from Object where ra_PS MOD 3 > 1.5",
        "select objectId, ra_PS - 3, decl_PS from Object where ra_PS - 3 > 1.5",
        [](query::SelectStmt::Ptr const & selectStatement) {
            // change the subtraction values expr to modulo:
            query::SelectList& selectList = selectStatement->getSelectList();
            (*selectList.getValueExprList())[1]->getFactorOpsRef()[0].op = query::ValueExpr::MOD;
            auto whereClauseRef = selectStatement->getWhereClause();
            auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(whereClauseRef.getRootTerm());
            auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
            auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
            auto compPredicate = std::dynamic_pointer_cast<query::CompPredicate>(boolFactor->_terms[0]);
            query::ValueExpr::FactorOp& factorOp = compPredicate->left->getFactorOpsRef()[0];
            factorOp.op = query::ValueExpr::MOD;
        },
        "SELECT objectId,(ra_PS MOD 3),decl_PS FROM Object WHERE (ra_PS MOD 3)>1.5"
    ),

    // tests the DIV operator
    Antlr4CompareQueries(
        "SELECT objectId from Object where ra_PS DIV 2 > 1",
        "SELECT objectId from Object where ra_PS/2 > 1",
        [](query::SelectStmt::Ptr const & selectStatement) {
              auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(selectStatement->getWhereClause().getRootTerm());
              auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
              auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
              auto compPredicate = std::dynamic_pointer_cast<query::CompPredicate>(boolFactor->_terms[0]);
              query::ValueExpr::FactorOp& factorOp = compPredicate->left->getFactorOpsRef()[0];
              factorOp.op = query::ValueExpr::DIV;
        },
        "SELECT objectId FROM Object WHERE (ra_PS DIV 2)>1"
    ),

    // tests the & operator
    Antlr4CompareQueries(
        "SELECT objectId from Object where objectID & 1 = 1",
        "SELECT objectId from Object where objectID = 1",
        [](query::SelectStmt::Ptr const & selectStatement) {
              auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(selectStatement->getWhereClause().getRootTerm());
              auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
              auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
              auto compPredicate = std::dynamic_pointer_cast<query::CompPredicate>(boolFactor->_terms[0]);
              compPredicate->left = std::make_shared<query::ValueExpr>(query::ValueExpr::FactorOpVector({
                  query::ValueExpr::FactorOp(
                          query::ValueFactor::newColumnRefFactor(query::ColumnRef::newShared("", "", "objectID")),
                          query::ValueExpr::BIT_AND),
                  query::ValueExpr::FactorOp(query::ValueFactor::newConstFactor("1"))
              }));
        },
        "SELECT objectId FROM Object WHERE (objectID&1)=1"
    ),

    // tests the | operator
    Antlr4CompareQueries(
        "SELECT objectId from Object where objectID | 1 = 1",
        "SELECT objectId from Object where objectID = 1",
        [](query::SelectStmt::Ptr const & selectStatement) {
              auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(selectStatement->getWhereClause().getRootTerm());
              auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
              auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
              auto compPredicate = std::dynamic_pointer_cast<query::CompPredicate>(boolFactor->_terms[0]);
              compPredicate->left = std::make_shared<query::ValueExpr>(query::ValueExpr::FactorOpVector({
                  query::ValueExpr::FactorOp(
                          query::ValueFactor::newColumnRefFactor(query::ColumnRef::newShared("", "", "objectID")),
                          query::ValueExpr::BIT_OR),
                  query::ValueExpr::FactorOp(query::ValueFactor::newConstFactor("1"))
              }));
        },
        "SELECT objectId FROM Object WHERE (objectID|1)=1"
    ),

    // tests the >> operator
    Antlr4CompareQueries(
        "SELECT objectId from Object where objectID << 10 = 1",
        "SELECT objectId from Object where objectID = 1",
        [](query::SelectStmt::Ptr const & selectStatement) {
              auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(selectStatement->getWhereClause().getRootTerm());
              auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
              auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
              auto compPredicate = std::dynamic_pointer_cast<query::CompPredicate>(boolFactor->_terms[0]);
              compPredicate->left = std::make_shared<query::ValueExpr>(query::ValueExpr::FactorOpVector({
                  query::ValueExpr::FactorOp(
                          query::ValueFactor::newColumnRefFactor(query::ColumnRef::newShared("", "", "objectID")),
                          query::ValueExpr::BIT_SHIFT_LEFT),
                  query::ValueExpr::FactorOp(query::ValueFactor::newConstFactor("10"))
              }));
        },
        "SELECT objectId FROM Object WHERE (objectID<<10)=1"
    ),

    // tests the << operator
    Antlr4CompareQueries(
        "SELECT objectId from Object where objectID >> 10 = 1",
        "SELECT objectId from Object where objectID = 1",
        [](query::SelectStmt::Ptr const & selectStatement) {
              auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(selectStatement->getWhereClause().getRootTerm());
              auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
              auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
              auto compPredicate = std::dynamic_pointer_cast<query::CompPredicate>(boolFactor->_terms[0]);
              compPredicate->left = std::make_shared<query::ValueExpr>(query::ValueExpr::FactorOpVector({
                  query::ValueExpr::FactorOp(
                          query::ValueFactor::newColumnRefFactor(query::ColumnRef::newShared("", "", "objectID")),
                          query::ValueExpr::BIT_SHIFT_RIGHT),
                  query::ValueExpr::FactorOp(query::ValueFactor::newConstFactor("10"))
              }));
        },
        "SELECT objectId FROM Object WHERE (objectID>>10)=1"
    ),

    // tests the ^ operator
    Antlr4CompareQueries(
        "SELECT objectId from Object where objectID ^ 1 = 1",
        "SELECT objectId from Object where objectID = 1",
        [](query::SelectStmt::Ptr const & selectStatement) {
              auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(selectStatement->getWhereClause().getRootTerm());
              auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
              auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
              auto compPredicate = std::dynamic_pointer_cast<query::CompPredicate>(boolFactor->_terms[0]);
              compPredicate->left = std::make_shared<query::ValueExpr>(query::ValueExpr::FactorOpVector({
                  query::ValueExpr::FactorOp(
                          query::ValueFactor::newColumnRefFactor(query::ColumnRef::newShared("", "", "objectID")),
                          query::ValueExpr::BIT_XOR),
                  query::ValueExpr::FactorOp(query::ValueFactor::newConstFactor("1"))
              }));
        },
        "SELECT objectId FROM Object WHERE (objectID^1)=1"
    ),

    // tests NOT with a BoolFactor
    Antlr4CompareQueries(
        "select * from Filter where NOT filterId > 1 AND filterId < 6",
        "select * from Filter where filterId > 1 AND filterId < 6",
        [](query::SelectStmt::Ptr const & selectStatement) {
            // flip the 'not' on the filterId boolFactor to make it 'not'
            auto whereClauseRef = selectStatement->getWhereClause();
            auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(whereClauseRef.getRootTerm());
            auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
            auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
            boolFactor->setHasNot(true);
        },
        "SELECT * FROM Filter WHERE NOT filterId>1 AND filterId<6"
    ),

    // tests NOT with an AND term
    Antlr4CompareQueries(
        "select * from Filter where NOT (filterId > 1 AND filterId < 6)",
        "select * from Filter where (filterId > 1 AND filterId < 6)",
        [](query::SelectStmt::Ptr const & selectStatement) {
            // flip the 'not' on the AndTerm to make it 'not'
            auto whereClauseRef = selectStatement->getWhereClause();
            auto orTerm = std::dynamic_pointer_cast<query::OrTerm>(whereClauseRef.getRootTerm());
            auto andTerm = std::dynamic_pointer_cast<query::AndTerm>(orTerm->_terms[0]);
            auto boolFactor = std::dynamic_pointer_cast<query::BoolFactor>(andTerm->_terms[0]);
            boolFactor->setHasNot(true);
        },
        "SELECT * FROM Filter WHERE NOT(filterId>1 AND filterId<6)"
    ),
};


BOOST_DATA_TEST_CASE(antlr4_compare, ANTLR4_COMPARE_QUERIES, queryInfo) {
    query::SelectStmt::Ptr selectStatement;
    BOOST_REQUIRE_NO_THROW(
        selectStatement = parser::SelectParser::makeSelectStmt(queryInfo.query));
    BOOST_REQUIRE(selectStatement != nullptr);

    query::SelectStmt::Ptr compSelectStatement;
    BOOST_REQUIRE_NO_THROW(
        compSelectStatement = parser::SelectParser::makeSelectStmt(queryInfo.compQuery));
    BOOST_REQUIRE(compSelectStatement != nullptr);

    if (queryInfo.modFunc != nullptr) {
        queryInfo.modFunc(compSelectStatement);
    }

    // verify the selectStatements are now the same:
    BOOST_REQUIRE_EQUAL(*selectStatement, *compSelectStatement);
    BOOST_TEST_MESSAGE("antlr4 selectStmt structure:" << *selectStatement);
    // verify the selectStatement converted back to sql is the same as the original query:
    BOOST_REQUIRE_EQUAL(selectStatement->getQueryTemplate().sqlFragment(),
            (queryInfo.serializedQuery != "" ? queryInfo.serializedQuery : queryInfo.query));

}


struct ParseErrorQueryInfo {
    ParseErrorQueryInfo(std::string const & q, std::string const & m)
    : query(q), errorMessage(m)
    {}

    std::string query;
    std::string errorMessage;
};

std::ostream& operator<<(std::ostream& os, ParseErrorQueryInfo const& i) {
    os << "ParseErrorQueryInfo(" << i.query << ", " << i.errorMessage << ")";
    return os;
}


static const std::vector< ParseErrorQueryInfo > PARSE_ERROR_QUERIES = {
    // "UNION JOIN" is not expected to parse.
    ParseErrorQueryInfo(
        "SELECT s1.foo, s2.foo AS s2_foo FROM Source s1 UNION JOIN Source s2 WHERE s1.bar = s2.bar;",
        "ParseException:Failed to instantiate query: \"SELECT s1.foo, s2.foo AS s2_foo FROM Source s1 UNION "
        "JOIN Source s2 WHERE s1.bar = s2.bar;\""),

    // The qserv manual says:
    // "Expressions/functions in ORDER BY clauses are not allowed
    // In SQL92 ORDER BY is limited to actual table columns, thus expressions or functions in ORDER BY are
    // rejected. This is true for Qserv too.
    ParseErrorQueryInfo(
        "SELECT objectId, iE1_SG, ABS(iE1_SG) FROM Object WHERE iE1_SG between -0.1 and 0.1 ORDER BY ABS(iE1_SG)",
        "ParseException:Error parsing query, near \"ABS(iE1_SG)\", qserv does not support functions in ORDER BY."),

    ParseErrorQueryInfo(
        "SELECT foo from Filter f limit 5 garbage query !#$%!#$",
        "ParseException:Failed to instantiate query: \"SELECT foo from Filter f limit 5 garbage query !#$%!#$\""),

    ParseErrorQueryInfo(
        "SELECT foo from Filter f limit 5 garbage query !#$%!#$",
        "ParseException:Failed to instantiate query: \"SELECT foo from Filter f limit 5 garbage query !#$%!#$\""),

    ParseErrorQueryInfo(
        "SELECT foo from Filter f limit 5; garbage query !#$%!#$",
        "ParseException:Failed to instantiate query: \"SELECT foo from Filter f limit 5; garbage query !#$%!#$\""),

    ParseErrorQueryInfo(
        "SELECT count(*) AS n, AVG(ra_PS), AVG(decl_PS), _chunkId FROM Object GROUP BY _chunkId;",
        "ParseException:Error parsing query, near \"_chunkId\", Identifiers in Qserv may not start with an underscore."),

    ParseErrorQueryInfo(
        "LECT sce.filterName,sce.field "
            "FROM LSST.Science_Ccd_Exposure AS sce "
            "WHERE sce.field=535 AND sce.camcol LIKE '%' ",
        "ParseException:Failed to instantiate query: \"LECT sce.filterName,sce.field "
            "FROM LSST.Science_Ccd_Exposure AS sce WHERE sce.field=535 AND sce.camcol LIKE '%' \""),

    // per testQueryAnaGeneral: CASE in column spec is illegal.
    ParseErrorQueryInfo(
        "SELECT  COUNT(*) AS totalCount, "
           "SUM(CASE WHEN (typeId=3) THEN 1 ELSE 0 END) AS galaxyCount "
           "FROM Object WHERE rFlux_PS > 10;",
       "ParseException:qserv can not parse query, near \"CASE WHEN (typeId=3) THEN 1 ELSE 0 END\""),
};


BOOST_DATA_TEST_CASE(expected_parse_error, PARSE_ERROR_QUERIES, queryInfo) {
    auto querySession = qproc::QuerySession();
    auto selectStmt = querySession.parseQuery(queryInfo.query);
    BOOST_REQUIRE_EQUAL(selectStmt, nullptr);
    BOOST_REQUIRE_EQUAL(querySession.getError(), queryInfo.errorMessage);
}

BOOST_AUTO_TEST_CASE(testUserQueryType) {
    using lsst::qserv::ccontrol::UserQueryType;

    BOOST_CHECK(UserQueryType::isSelect("SELECT 1"));
    BOOST_CHECK(UserQueryType::isSelect("SELECT\t1"));
    BOOST_CHECK(UserQueryType::isSelect("SELECT\n\r1"));

    BOOST_CHECK(UserQueryType::isSelect("select 1"));
    BOOST_CHECK(UserQueryType::isSelect("SeLeCt 1"));

    BOOST_CHECK(not UserQueryType::isSelect("unselect X"));
    BOOST_CHECK(not UserQueryType::isSelect("DROP SELECT;"));

    std::string stripped;
    BOOST_CHECK(UserQueryType::isSubmit("SUBMIT SELECT", stripped));
    BOOST_CHECK_EQUAL("SELECT", stripped);
    BOOST_CHECK(UserQueryType::isSubmit("submit\tselect  ", stripped));
    BOOST_CHECK_EQUAL("select  ", stripped);
    BOOST_CHECK(UserQueryType::isSubmit("SubMiT \n SelEcT", stripped));
    BOOST_CHECK_EQUAL("SelEcT", stripped);
    BOOST_CHECK(not UserQueryType::isSubmit("submit", stripped));
    BOOST_CHECK(not UserQueryType::isSubmit("submit ", stripped));
    BOOST_CHECK(not UserQueryType::isSubmit("unsubmit select", stripped));
    BOOST_CHECK(not UserQueryType::isSubmit("submitting select", stripped));

    struct {
        const char* query;
        const char* db;
        const char* table;
    } drop_table_ok[] = {
        {"DROP TABLE DB.TABLE", "DB", "TABLE"},
        {"DROP TABLE DB.TABLE;", "DB", "TABLE"},
        {"DROP TABLE DB.TABLE ;", "DB", "TABLE"},
        {"DROP TABLE `DB`.`TABLE` ", "DB", "TABLE"},
        {"DROP TABLE \"DB\".\"TABLE\"", "DB", "TABLE"},
        {"DROP TABLE TABLE", "", "TABLE"},
        {"DROP TABLE `TABLE`", "", "TABLE"},
        {"DROP TABLE \"TABLE\"", "", "TABLE"},
        {"drop\ttable\nDB.TABLE ;", "DB", "TABLE"}
    };

    for (auto test: drop_table_ok) {
        std::string db, table;
        BOOST_CHECK(UserQueryType::isDropTable(test.query, db, table));
        BOOST_CHECK_EQUAL(db, test.db);
        BOOST_CHECK_EQUAL(table, test.table);
    }

    const char* drop_table_fail[] = {
        "DROP DATABASE DB",
        "DROP TABLE",
        "DROP TABLE TABLE; DROP IT;",
        "DROP TABLE 'DB'.'TABLE'",
        "DROP TABLE db%.TABLE",
        "UNDROP TABLE X"
    };
    for (auto test: drop_table_fail) {
        std::string db, table;
        BOOST_CHECK(not UserQueryType::isDropTable(test, db, table));
    }

    struct {
        const char* query;
        const char* db;
    } drop_db_ok[] = {
        {"DROP DATABASE DB", "DB"},
        {"DROP SCHEMA DB ", "DB"},
        {"DROP DATABASE DB;", "DB"},
        {"DROP SCHEMA DB ; ", "DB"},
        {"DROP DATABASE `DB` ", "DB"},
        {"DROP SCHEMA \"DB\"", "DB"},
        {"drop\tdatabase\nd_b ;", "d_b"}
    };
    for (auto test: drop_db_ok) {
        std::string db;
        BOOST_CHECK(UserQueryType::isDropDb(test.query, db));
        BOOST_CHECK_EQUAL(db, test.db);
    }

    const char* drop_db_fail[] = {
        "DROP TABLE DB",
        "DROP DB",
        "DROP DATABASE",
        "DROP DATABASE DB;;",
        "DROP SCHEMA DB; DROP IT;",
        "DROP SCHEMA DB.TABLE",
        "DROP SCHEMA 'DB'",
        "DROP DATABASE db%",
        "UNDROP DATABASE X",
        "UN DROP DATABASE X"
    };
    for (auto test: drop_db_fail) {
        std::string db;
        BOOST_CHECK(not UserQueryType::isDropDb(test, db));
    }

    struct {
        const char* query;
        const char* db;
    } flush_empty_ok[] = {
        {"FLUSH QSERV_CHUNKS_CACHE", ""},
        {"FLUSH QSERV_CHUNKS_CACHE\t ", ""},
        {"FLUSH QSERV_CHUNKS_CACHE;", ""},
        {"FLUSH QSERV_CHUNKS_CACHE ; ", ""},
        {"FLUSH QSERV_CHUNKS_CACHE FOR DB", "DB"},
        {"FLUSH QSERV_CHUNKS_CACHE FOR `DB`", "DB"},
        {"FLUSH QSERV_CHUNKS_CACHE FOR \"DB\"", "DB"},
        {"FLUSH QSERV_CHUNKS_CACHE FOR DB ; ", "DB"},
        {"flush qserv_chunks_cache for `d_b`", "d_b"},
        {"flush\nqserv_chunks_CACHE\tfor \t d_b", "d_b"},
    };
    for (auto test: flush_empty_ok) {
        std::string db;
        BOOST_CHECK(UserQueryType::isFlushChunksCache(test.query, db));
        BOOST_CHECK_EQUAL(db, test.db);
    }

    const char* flush_empty_fail[] = {
        "FLUSH QSERV CHUNKS CACHE",
        "UNFLUSH QSERV_CHUNKS_CACHE",
        "FLUSH QSERV_CHUNKS_CACHE DB",
        "FLUSH QSERV_CHUNKS_CACHE FOR",
        "FLUSH QSERV_CHUNKS_CACHE FROM DB",
        "FLUSH QSERV_CHUNKS_CACHE FOR DB.TABLE",
    };
    for (auto test: flush_empty_fail) {
        std::string db;
        BOOST_CHECK(not UserQueryType::isFlushChunksCache(test, db));
    }

    const char* show_proclist_ok[] = {
        "SHOW PROCESSLIST",
        "show processlist",
        "show    PROCESSLIST",
    };
    for (auto test: show_proclist_ok) {
        bool full;
        BOOST_CHECK(UserQueryType::isShowProcessList(test, full));
        BOOST_CHECK(not full);
    }

    const char* show_full_proclist_ok[] = {
        "SHOW FULL PROCESSLIST",
        "show full   processlist",
        "show FULL PROCESSLIST",
    };
    for (auto test: show_full_proclist_ok) {
        bool full;
        BOOST_CHECK(UserQueryType::isShowProcessList(test, full));
        BOOST_CHECK(full);
    }

    const char* show_proclist_fail[] = {
        "show PROCESS",
        "SHOW PROCESS LIST",
        "show fullprocesslist",
        "show full process list",
    };
    for (auto test: show_proclist_fail) {
        bool full;
        BOOST_CHECK(not UserQueryType::isShowProcessList(test, full));
    }

    struct {
        const char* db;
        const char* table;
    } proclist_table_ok[] = {
        {"INFORMATION_SCHEMA", "PROCESSLIST"},
        {"information_schema", "processlist"},
        {"Information_Schema", "ProcessList"},
    };
    for (auto test: proclist_table_ok) {
        BOOST_CHECK(UserQueryType::isProcessListTable(test.db, test.table));
    }

    struct {
        const char* db;
        const char* table;
    } proclist_table_fail[] = {
        {"INFORMATIONSCHEMA", "PROCESSLIST"},
        {"information_schema", "process_list"},
        {"Information Schema", "Process List"},
    };
    for (auto test: proclist_table_fail) {
        BOOST_CHECK(not UserQueryType::isProcessListTable(test.db, test.table));
    }

    struct {
        const char* query;
        int id;
    } kill_query_ok[] = {
        {"KILL 100", 100},
        {"KilL 101  ", 101},
        {"kill   102  ", 102},
        {"KILL QUERY 100", 100},
        {"kill\tquery   100   ", 100},
        {"KILL CONNECTION 100", 100},
        {"KILL \t CONNECTION   100  ", 100},
    };
    for (auto test: kill_query_ok) {
        int threadId;
        BOOST_CHECK(UserQueryType::isKill(test.query, threadId));
        BOOST_CHECK_EQUAL(threadId, test.id);
    }

    const char* kill_query_fail[] = {
        "NOT KILL 100",
        "KILL SESSION 100 ",
        "KILL QID100",
        "KILL 100Q ",
        "KILL QUIERY=100 ",
    };
    for (auto test: kill_query_fail) {
        int threadId;
        BOOST_CHECK(not UserQueryType::isKill(test, threadId));
    }

    struct {
        const char* query;
        QueryId id;
    } cancel_ok[] = {
        {"CANCEL 100", 100},
        {"CAnCeL 101  ", 101},
        {"cancel \t  102  ", 102},
    };
    for (auto test: cancel_ok) {
        QueryId queryId;
        BOOST_CHECK(UserQueryType::isCancel(test.query, queryId));
        BOOST_CHECK_EQUAL(queryId, test.id);
    }

    const char* cancel_fail[] = {
        "NOT CANCLE 100",
        "CANCEL QUERY 100 ",
        "CANCEL q100",
        "cancel 100Q ",
        "cancel QUIERY=100 ",
    };
    for (auto test: cancel_fail) {
        QueryId queryId;
        BOOST_CHECK(not UserQueryType::isCancel(test, queryId));
    }

}

BOOST_AUTO_TEST_SUITE_END()
