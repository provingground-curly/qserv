/*
 * LSST Data Management System
 * Copyright 2017 LSST Corporation.
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

#include "replica_core/DatabaseMySQL.h"

// System headers

#include <boost/lexical_cast.hpp>
#include <mysql/mysqld_error.h>
#include <sstream>

// Qserv headers

#include "lsst/log/Log.h"

namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica_core.DatabaseMySQL");

/**
 * The function is used to comply with the MySQL convention for
 * the default values of the connection parameters.
 */
char const* stringOrNull (std::string const& str) {
    if (str.empty()) return 0;
    return str.c_str();
}

using Row              = lsst::qserv::replica_core::database::mysql::Row;
using InvalidTypeError = lsst::qserv::replica_core::database::mysql::InvalidTypeError;


template <typename K>
bool getAsString (Row const&   row,
                  K            key,
                  std::string& value) {

    Row::Cell const& cell = row.getDataCell (key);
    if (cell.first) {
        value = std::string(cell.first);
        return true;
    }
    return false;
}

template <typename K, class T>
bool getAsNumber (Row const& row,
                  K          key,
                  T&         value) {
    try {
        Row::Cell const& cell = row.getDataCell (key);
        if (cell.first) {
            value = boost::lexical_cast<uint64_t>(cell.first, cell.second);
            return true;
        }
        return false;
    } catch (boost::bad_lexical_cast const& ex) {
        throw InvalidTypeError ("DatabaseMySQL::getAsNumber<K,T>()  type conversion failed");
    }
}


}   // namespace


namespace lsst {
namespace qserv {
namespace replica_core {
namespace database {
namespace mysql {


/////////////////////////////////////////////////////
//                ConnectionParams                 //
/////////////////////////////////////////////////////

std::string
ConnectionParams::toString () const {
    std::ostringstream ss;
    ss << *this;
    return ss.str();
}

std::ostream& operator<< (std::ostream& os, ConnectionParams const& params) {
    os  << "DatabaseMySQL::ConnectionParams "
        << "(host="     << params.host
        << " port="     << params.port
        << " database=" << params.database
        << " user="     << params.user
        << " password=*****)";
    return os;
}


///////////////////////////////////////
//                Row                //
///////////////////////////////////////

Row::Row ()
    :   _isValid (false) {
}

Row::Row (Row const& rhs) {
    _isValid    = rhs._isValid;
    _name2index = rhs._name2index;
    _index2cell = rhs._index2cell;   
}

Row&
Row::operator= (Row const& rhs) {
    if (this != &rhs) {
        _isValid    = rhs._isValid;
        _name2index = rhs._name2index;   
        _index2cell = rhs._index2cell;   
    }
    return *this;
}

Row::~Row () {
}


size_t
Row::numColumns () const {
    static std::string const context = "Row::numColumns()  ";
    if (!_isValid) throw std::logic_error (context + "the object is not valid");
    return  _index2cell.size();
}

bool Row::isNull (size_t              columnIdx) const { return !getDataCell (columnIdx) .first; }
bool Row::isNull (std::string const& columnName) const { return !getDataCell (columnName).first; }

bool Row::get (size_t             columnIdx,  std::string& value) const { return ::getAsString (*this, columnIdx,  value); }
bool Row::get (std::string const& columnName, std::string& value) const { return ::getAsString (*this, columnName, value); }

bool Row::get (size_t columnIdx, uint64_t& value) const { return ::getAsNumber (*this, columnIdx, value); }
bool Row::get (size_t columnIdx, uint32_t& value) const { return ::getAsNumber (*this, columnIdx, value); }
bool Row::get (size_t columnIdx, uint16_t& value) const { return ::getAsNumber (*this, columnIdx, value); }
bool Row::get (size_t columnIdx, uint8_t&  value) const { return ::getAsNumber (*this, columnIdx, value); }

bool Row::get (std::string const& columnName, uint64_t& value) const { return ::getAsNumber (*this, columnName, value); }
bool Row::get (std::string const& columnName, uint32_t& value) const { return ::getAsNumber (*this, columnName, value); }
bool Row::get (std::string const& columnName, uint16_t& value) const { return ::getAsNumber (*this, columnName, value); }
bool Row::get (std::string const& columnName, uint8_t&  value) const { return ::getAsNumber (*this, columnName, value); }

bool Row::get (size_t columnIdx, int64_t& value) const { return ::getAsNumber (*this, columnIdx, value); }
bool Row::get (size_t columnIdx, int32_t& value) const { return ::getAsNumber (*this, columnIdx, value); }
bool Row::get (size_t columnIdx, int16_t& value) const { return ::getAsNumber (*this, columnIdx, value); }
bool Row::get (size_t columnIdx, int8_t&  value) const { return ::getAsNumber (*this, columnIdx, value); }

bool Row::get (std::string const& columnName, int64_t& value) const { return ::getAsNumber (*this, columnName, value); }
bool Row::get (std::string const& columnName, int32_t& value) const { return ::getAsNumber (*this, columnName, value); }
bool Row::get (std::string const& columnName, int16_t& value) const { return ::getAsNumber (*this, columnName, value); }
bool Row::get (std::string const& columnName, int8_t&  value) const { return ::getAsNumber (*this, columnName, value); }

bool Row::get (size_t columnIdx, float&  value) const { return ::getAsNumber (*this, columnIdx, value); }
bool Row::get (size_t columnIdx, double& value) const { return ::getAsNumber (*this, columnIdx, value); }

bool Row::get (std::string const& columnName, float&  value) const { return ::getAsNumber (*this, columnName, value); }
bool Row::get (std::string const& columnName, double& value) const { return ::getAsNumber (*this, columnName, value); }


Row::Cell const&
Row::getDataCell (size_t columnIdx) const {

    static std::string const context = "Row::getDataCell()  ";

    if (!_isValid) throw std::logic_error (context + "the object is not valid");

    if (columnIdx >= _index2cell.size())
        throw std::invalid_argument (
                context + "the column index '" + std::to_string(columnIdx) +
                "'is not in the result set");
    
    return _index2cell.at(columnIdx);
}

Row::Cell const&
Row::getDataCell (std::string const& columnName) const {

    static std::string const context = "Row::getDataCell()  ";

    if (!_isValid) throw std::logic_error (context + "the object is not valid");

    if (!_name2index.count(columnName))
        throw std::invalid_argument (
                context + "the column '" + columnName + "'is not in the result set");
    
    return _index2cell.at(_name2index.at(columnName));
}

/////////////////////////////////////////////
//                Function                 //
/////////////////////////////////////////////

Function const Function::LAST_INSERT_ID {"LAST_INSERT_ID()"};

Function::Function (std::string const& name_)
    :   name (name_) {
}

///////////////////////////////////////////////
//                Connection                 //
///////////////////////////////////////////////

Connection::pointer
Connection::open (ConnectionParams const& connectionParams,
                  bool                    autoReconnect) {

    Connection::pointer ptr (new Connection(connectionParams,
                                            autoReconnect));
    ptr->connect();
    return ptr;
}

Connection::Connection (ConnectionParams const& connectionParams,
                        bool                    autoReconnect)
    :   _connectionParams (connectionParams),
        _autoReconnect    (autoReconnect),

        _inTransaction (false),

        _mysql  (nullptr),
        _res    (nullptr),
        _fields (nullptr),

        _numFields (0) {
}


Connection::~Connection () {
    if (_res) mysql_free_result (_res) ;
}

std::string
Connection::escape (std::string const& inStr) const {

    static std::string const context = "Connection::escape()  ";

    if (!_mysql) throw Error (context + "not connected to the MySQL service");

    size_t const inLen = inStr.length ();

    // Allocate at least that number of bytes to cover the worst case scenario
    // of each input character to be escaped plus the end of string terminator.
    // See: https://dev.mysql.com/doc/refman/5.7/en/mysql-real-escape-string.html

    size_t const outLenMax = 2*inLen + 1;

    // The temporary storage will get automatically deconstructed in the end
    // of this block.

    std::unique_ptr<char> outStr(new char[outLenMax]);
    size_t const outLen =
        mysql_real_escape_string (
            _mysql,
            outStr.get(),
            inStr.c_str(),
            inLen);

    return std::string (outStr.get(), outLen) ;
}

Connection::pointer
Connection::begin () {
    assertTransaction (false);
    execute ("BEGIN");
    _inTransaction = true;
    return shared_from_this();
}


Connection::pointer
Connection::commit () {
    assertTransaction (true);
    execute ("COMMIT");
    _inTransaction = false;
    return shared_from_this();
}


Connection::pointer
Connection::rollback () {
    assertTransaction (true);
    execute ("ROLLBACK");
    _inTransaction = false;
    return shared_from_this();
}

Connection::pointer
Connection::execute (std::string const& query) {

    static std::string const context = "Connection::execute()  ";

    LOGS(_log, LOG_LVL_DEBUG, context + query);

    if (query.empty())
        throw std::invalid_argument (
                context + "empty query string passed into the object");

    // Reset/initialize the query context before attempting to execute
    // the new  query.

    _lastQuery = query;

    if (_res) mysql_free_result (_res) ;
    _res       = nullptr;
    _fields    = nullptr;
    _numFields = 0;

    _columnNames.clear();

    if (mysql_real_query (_mysql,
                          _lastQuery.c_str(),
                          _lastQuery.size())) {

        std::string const msg =
            context + "query: '" + _lastQuery + "', error: " +
            std::string(mysql_error(_mysql));

        switch (mysql_errno(_mysql)) {
            case ER_DUP_ENTRY: throw DuplicateKeyError (msg);
            default:           throw Error             (msg);
        }
    }
    
    // Fetch result set for queries which return the one

    if (mysql_field_count (_mysql)) {

        // unbuffered read
        _res = mysql_use_result (_mysql);
        if (!_res)
            throw Error (context + "mysql_use_result failed, error: " +
                         std::string(mysql_error(_mysql)));

        _fields    = mysql_fetch_fields (_res);
        _numFields = mysql_num_fields   (_res);

        for (size_t i = 0; i < _numFields; i++) {
            _columnNames.push_back(std::string(_fields[i].name));
        }
    }
    return shared_from_this();
}

bool
Connection::hasResult () const {
    return _mysql && _res;
}

std::vector<std::string> const&
Connection::columnNames () const {
    assertQueryContext ();
    return _columnNames;
}

bool
Connection::next (Row& row) {

    static std::string const context = "Connection::next()  ";

    assertQueryContext ();

    _row = mysql_fetch_row (_res);
    if (!_row) {
        if (!mysql_errno(_mysql)) return false;
        throw Error (context + "mysql_fetch_row failed, error: " +
                     std::string(mysql_error(_mysql)) +
                     ", query: '" + _lastQuery + "'");
    }
    size_t const* lengths = mysql_fetch_lengths (_res);

    // Transfer the data pointers for each field and their lengths into
    // the provided Row object.

    row._isValid = true;
    row._index2cell.reserve (_numFields);
    for (size_t i = 0; i < _numFields; i++) {
        row._name2index[_fields[i].name] = i;
        row._index2cell.emplace_back (Row::Cell {_row[i], lengths[i]});
    }
    return true;
}

void
Connection::connect () {

    static std::string const context = "Connection::connect()  ";

    // Prepare the connection object
    if (!(_mysql = mysql_init (_mysql)))
        throw Error(context + "mysql_init failed");
    
    // Allow automatic reconnect if requested
    if (_autoReconnect) {
        my_bool reconnect = 0;
        mysql_options(_mysql, MYSQL_OPT_RECONNECT, &reconnect);
    }

    // Connect now
    if (!mysql_real_connect (
        _mysql,
        ::stringOrNull (_connectionParams.host),
        ::stringOrNull (_connectionParams.user),
        ::stringOrNull (_connectionParams.password),
        ::stringOrNull (_connectionParams.database),
        _connectionParams.port,
        0,  /* no default UNIX socket */
        0)) /* no default client flag */
        throw Error (context + "mysql_real_connect() failed, error: " +
                     std::string(mysql_error(_mysql)));

    // Set session attributes
    if (mysql_query (_mysql, "SET SESSION SQL_MODE='ANSI'") ||
        mysql_query (_mysql, "SET SESSION AUTOCOMMIT=0"))
        throw Error (context + "mysql_query() failed, error: " +
                     std::string(mysql_error(_mysql)));
}

void
Connection::assertQueryContext () const {

    static std::string const context = "Connection::assertQueryContext()  ";

    if (!_mysql) throw Error (context + "not connected to the MySQL service");
    if (!_res)   throw Error (context + "no prior query made");
}

void
Connection::assertTransaction (bool inTransaction) const {

    static std::string const context = "Connection::assertTransaction()  ";

    if (inTransaction != _inTransaction)
        throw std::logic_error (
                context + "the transaction is" +
                std::string( _inTransaction ? " " : " not") + " active");
}

}}}}} // namespace lsst::qserv::replica_core::database::mysql
