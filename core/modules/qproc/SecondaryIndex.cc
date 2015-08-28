// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2014-2015 AURA/LSST.
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
  * @brief SecondaryIndex implementation
  *
  * @author Daniel L. Wang, SLAC
  */

#include "qproc/SecondaryIndex.h"

// System headers
#include <algorithm>

// LSST headers
#include "lsst/log/Log.h"

// Third-party headers
#include "boost/format.hpp"

// Qserv headers
#include "global/Bug.h"
#include "global/intTypes.h"
#include "global/constants.h"
#include "global/stringUtil.h"
#include "qproc/ChunkSpec.h"
#include "query/Constraint.h"
#include "sql/SqlConnection.h"
#include "util/IterableFormatter.h"

namespace lsst {
namespace qserv {
namespace qproc {

namespace {

LOG_LOGGER getLogger() {
    static LOG_LOGGER logger = LOG_GET("lsst.qserv.qproc.SecondaryIndex");
    return logger;
}

} // anonymous namespace

class SecondaryIndex::Backend {
public:
    virtual ~Backend() {}
    /// Lookup an index constraint. Ignore constraints that are not "sIndex"
    /// constraints.
    virtual ChunkSpecVector lookup(query::ConstraintVector const& cv) = 0;
};

class MySqlBackend : public SecondaryIndex::Backend {
public:
    MySqlBackend(mysql::MySqlConfig const& c)
        : _sqlConnection(c, true) {
    }

    virtual ChunkSpecVector lookup(query::ConstraintVector const& cv) {
        ChunkSpecVector output;
        bool hasIndex = false;
        for(query::ConstraintVector::const_iterator i=cv.begin(), e=cv.end();
            i != e;
            ++i) {
            if (i->name == "sIndex") {
                _sqlLookup(output, *i);
                hasIndex = true;
            }
        }
        if(!hasIndex) {
            throw SecondaryIndex::NoIndexConstraint();
        }
        normalize(output);
        return output;
    }


private:


    static std::string _buildIndexTableName(
        std::string const& db,
        std::string const& table
    ) {
        return (std::string(SEC_INDEX_DB) + "."
                + sanitizeName(db) + "__" + sanitizeName(table));
    }

    /**
     *  Build sql query string to run against secondary index
     *
     *  @param:    vector of string used to build the query,
     *             format is:
     *             [db, table, keyColumn, id_0, ..., id_n]
     *             where:
     *             - db.table is the director table,
     *             - keyColumn is its primary key,
     *             - id_x are keyColumn values
     *
     *  @return:   the sql query string to run against secondary index in
     *             order to get (chunks, subchunks) couples containing [id_0, ..., id_n]
     */
    static std::string _buildLookupQuery(
        std::vector<std::string> const& params
    ) {

        char const LOOKUP_SQL_TEMPLATE[] = "SELECT %s, %s FROM %s WHERE %s IN (%s);";
        LOGF(getLogger(), LOG_LVL_TRACE, "params: %s" % util::formatable(params));

        std::string const& db = params[0];
        std::string const& table = params[1];
        std::string const& keyColumn = params[2];

        char const *const empty_bracket = "";
        auto idsFormatter = util::formatable(params, 3, empty_bracket, empty_bracket);
        std::string sql = (boost::format(LOOKUP_SQL_TEMPLATE) % CHUNK_COLUMN
                                                              % SUB_CHUNK_COLUMN
                                                              % _buildIndexTableName(db, table)
                                                              % keyColumn
                                                              % idsFormatter).str();
        LOGF(getLogger(), LOG_LVL_TRACE, "sql: %s" % sql);
        return sql;
    }

    void _sqlLookup(ChunkSpecVector& output, query::Constraint const& c) {
            IntVector ids;
            if (c.name != "sIndex") {
                throw Bug("Unexpected non-index constraint");
            }
            std::string sql = _buildLookupQuery(c.params);
            ChunkSpecMap m;
            for(std::shared_ptr<sql::SqlResultIter> results = _sqlConnection.getQueryIter(sql);
                not results->done();
                ++(*results)) {
                StringVector const& row = **results;
                int chunkId = std::stoi(row[0]);
                int subChunkId = std::stoi(row[1]);
                ChunkSpecMap::iterator e = m.find(chunkId);
                if (e == m.end()) {
                    ChunkSpec& cs = m[chunkId];
                    cs.chunkId = chunkId;
                    cs.subChunks.push_back(subChunkId);
                } else {
                    ChunkSpec& cs = e->second;
                    cs.subChunks.push_back(subChunkId);
                }
            }
            for(ChunkSpecMap::const_iterator i=m.begin(), e=m.end();
                i != e; ++i) {
                output.push_back(i->second);
            }
    }

    sql::SqlConnection _sqlConnection;
};

class FakeBackend : public SecondaryIndex::Backend {
public:
    FakeBackend() {}
    virtual ChunkSpecVector lookup(query::ConstraintVector const& cv) {
        ChunkSpecVector dummy;
        if(_hasSecondary(cv)) {
            for(int i=100; i < 103; ++i) {
                int bogus[] = {1,2,3};
                std::vector<int> subChunks(bogus, bogus+3);
                dummy.push_back(ChunkSpec(i, subChunks));
            }
        }
        return dummy;
    }
private:
    struct _checkIndex {
        bool operator()(query::Constraint const& c) {
            return c.name == "sIndex"; }
    };
    bool _hasSecondary(query::ConstraintVector const& cv) {
        return cv.end() != std::find_if(cv.begin(), cv.end(), _checkIndex());
    }
};

SecondaryIndex::SecondaryIndex(mysql::MySqlConfig const& c)
    : _backend(std::make_shared<MySqlBackend>(c)) {
}

SecondaryIndex::SecondaryIndex(int)
    : _backend(std::make_shared<FakeBackend>()) {
}

ChunkSpecVector SecondaryIndex::lookup(query::ConstraintVector const& cv) {
    if (_backend) {
        return _backend->lookup(cv);
    } else {
        throw Bug("SecondaryIndex : no backend initialized");
    }
}

}}} // namespace lsst::qserv::qproc

