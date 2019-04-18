// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2012-2016 AURA/LSST.
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


// Class header
#include "query/DbTablePair.h"

// Qserv headers
#include "query/TableAlias.h"


namespace lsst {
namespace qserv {
namespace query {


DbTablePair TableAliases::get(std::string const& alias) const {
    auto&& itr = tableAliasSet.get<ByAlias>().find(alias);
    if (itr != tableAliasSet.end()) {
        return itr->dbTablePair;
    }
    return DbTablePair();
}


void TableAliases::set(std::string const& db, std::string const& table, std::string const& alias) {
    tableAliasSet.insert(TableAlias(alias, DbTablePair(db, table)));
}


std::string const TableAliases::get(std::string db, std::string table) const {
    return get(DbTablePair(db, table));
}


std::string const TableAliases::get(DbTablePair const& dbTablePair) const {
    //TableAliasSet::index<ByDbTablePair>::type::const_iterator itr =
    auto&& itr = tableAliasSet.get<ByDbTablePair>().find(dbTablePair);
    if (itr != tableAliasSet.get<ByDbTablePair>().end()) {
        return itr->alias;
    }
    return std::string();
}



}}} // namespace lsst::qserv::query
