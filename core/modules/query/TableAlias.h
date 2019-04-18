// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2013-2019 LSST Corporation.
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


#ifndef LSST_QSERV_QUERY_TABLEALIAS_H
#define LSST_QSERV_QUERY_TABLEALIAS_H


// System headers
#include <map>
#include <sstream>
#include <stdexcept>

// Third-party headers
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>

// Local headers
#include "query/DbTablePair.h"


// forward declarations
namespace lsst {
namespace qserv {
namespace query {
    class TableRef;
}}}


namespace lsst {
namespace qserv {
namespace query {


class TableAliases {
public:
    TableAliases() = default;

    /// Add an alias for a given db + table
    bool set(std::string const& db, std::string const& table, std::string const& alias);

    /// Add an alias provided by a table ref
    bool set(std::shared_ptr<TableRef> const& tableRef);

    /// Get a db + table for an alias
    DbTablePair get(std::string const& alias) const;

    /// Get an alias for a db + table
    std::string const get(std::string db, std::string table) const;

    /// Get an alias for a DbTablePair (a.k.a. a db + table)
    std::string const get(DbTablePair const& dbTablePair) const;

    // nptodo ? might need to add support for an "ambiguous" lookup (or set?), something to do with the
    // db not being set, or the alias not being set? the impl in TableAliasReverse looks wrong or I don't
    // understand it yet. TBD if we run into this as an issue maybe it was never actually used.

private:
    struct TableAlias {
        std::string alias;
        DbTablePair dbTablePair;
        TableAlias(std::string alias_, DbTablePair dbTablePair_)
        : alias(alias_), dbTablePair(dbTablePair_)
        {}
    };

    struct ByAlias{};
    struct ByDbTablePair{};

    typedef boost::multi_index::multi_index_container<
        TableAlias,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<ByAlias>,
                boost::multi_index::member<TableAlias, std::string, &TableAlias::alias>
            >,
            // ordered_unique here assumes exactly 1 alias for any database + table pair.
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<ByDbTablePair>,
                boost::multi_index::member<TableAlias, DbTablePair, &TableAlias::dbTablePair>
            >
        >
    > TableAliasSet;

    TableAliasSet tableAliasSet;
};


}}} // namespace lsst::qserv::query

#endif // LSST_QSERV_QUERY_TABLEALIAS_H
