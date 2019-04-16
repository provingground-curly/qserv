// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2014-2016 AURA/LSST.
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
  * @brief QueryRunner instances perform single-shot query execution with the
  * result reflected in the db state or returned via a SendChannel. Works with
  * new XrdSsi API.
  *
  * @author Daniel L. Wang, SLAC; John Gates, SLAC
  */


// System headers
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <memory>

// Third-party headers
#include <boost/algorithm/string/replace.hpp>
#include <mysql/mysql.h>

// Class header
#include "wdb/QueryRunner.h"

// LSST headers
#include "lsst/log/Log.h"

// Qserv headers
#include "global/Bug.h"
#include "global/constants.h"
#include "global/DbTable.h"
#include "global/debugUtil.h"
#include "global/UnsupportedError.h"
#include "mysql/MySqlConfig.h"
#include "mysql/MySqlConnection.h"
#include "mysql/SchemaFactory.h"
#include "proto/ProtoHeaderWrap.h"
#include "proto/worker.pb.h"
#include "sql/Schema.h"
#include "sql/SqlErrorObject.h"
#include "util/common.h"
#include "util/IterableFormatter.h"
#include "util/MultiError.h"
#include "util/StringHash.h"
#include "util/Timer.h"
#include "util/threadSafe.h"
#include "wbase/Base.h"
#include "wbase/SendChannel.h"
#include "wdb/ChunkResource.h"

namespace {
LOG_LOGGER _log = LOG_GET("lsst.qserv.wdb.QueryRunner");
}

namespace lsst {
namespace qserv {
namespace wdb {


QueryRunner::Ptr QueryRunner::newQueryRunner(wbase::Task::Ptr const& task,
                                             ChunkResourceMgr::Ptr const& chunkResourceMgr,
                                             mysql::MySqlConfig const& mySqlConfig) {
    Ptr qr{new QueryRunner{task, chunkResourceMgr, mySqlConfig}}; // Private constructor.
    // Let the Task know this is its QueryRunner.
    bool cancelled = qr->_task->setTaskQueryRunner(qr);
    if (cancelled) {
        qr->_cancelled.store(true);
        // runQuery will return quickly if the Task has been cancelled.
    }
    return qr;
}

/// New instances need to be made with QueryRunner to ensure registration with the task
/// and correct setup of enable_shared_from_this.
QueryRunner::QueryRunner(wbase::Task::Ptr const& task,
                         ChunkResourceMgr::Ptr const& chunkResourceMgr,
                         mysql::MySqlConfig const& mySqlConfig)
    : _task(task), _chunkResourceMgr(chunkResourceMgr), _mySqlConfig(mySqlConfig) {
    int rc = mysql_thread_init();
    assert(rc == 0);
    assert(_task->msg);
}

/// Initialize the db connection
bool QueryRunner::_initConnection() {
    mysql::MySqlConfig localMySqlConfig(_mySqlConfig);
    localMySqlConfig.username = _task->user; // Override with czar-passed username.
    _mysqlConn.reset(new mysql::MySqlConnection(localMySqlConfig));

    if (not _mysqlConn->connect()) {
        LOGS(_log, LOG_LVL_ERROR, _task->getIdStr() << " Unable to connect to MySQL: " << localMySqlConfig);
        util::Error error(-1, "Unable to connect to MySQL; " + localMySqlConfig.toString());
        _multiError.push_back(error);
        return false;
    }
    return true;
}

/// Override _dbName with _msg->db() if available.
void QueryRunner::_setDb() {
    if (_task->msg->has_db()) {
        _dbName = _task->msg->db();
        LOGS(_log, LOG_LVL_WARN, "QueryRunner overriding dbName with " << _dbName);
    }
}


util::TimerHistogram memWaitHisto("memWait Hist", {1, 5, 10, 20, 40});


bool QueryRunner::runQuery() {
    LOGS(_log, LOG_LVL_DEBUG, _task->getIdStr() << " QueryRunner::runQuery()");

    // Make certain our Task knows that this object is no longer in use when this function exits.
    class Release {
    public:
        Release(wbase::Task::Ptr t, wbase::TaskQueryRunner *tqr) : _t{t}, _tqr{tqr} {}
        ~Release() { _t->freeTaskQueryRunner(_tqr); }
    private:
        wbase::Task::Ptr _t;
        wbase::TaskQueryRunner *_tqr;
    };
    Release release(_task, this);

    if (_task->getCancelled()) {
        LOGS(_log, LOG_LVL_DEBUG, _task->getIdStr() << " runQuery, task was cancelled before it started.");
        return false;
    }

    // Wait for memman to finish reserving resources. This can take several seconds.
    util::Timer memTimer;
    memTimer.start();
    _task->waitForMemMan();
    memTimer.stop();
    auto logMsg = memWaitHisto.addTime(memTimer.getElapsed(), _task->getIdStr());
    LOGS(_log, LOG_LVL_INFO, logMsg);

    if (_task->getCancelled()) {
        LOGS(_log, LOG_LVL_DEBUG, _task->getIdStr() << " runQuery, task was cancelled after locking tables.");
        return false;
    }

    _setDb();
    LOGS(_log, LOG_LVL_DEBUG,  _task->getIdStr() << " Exec in flight for Db=" << _dbName);
    bool connOk = _initConnection();
    if (!connOk) {
        // Transmit the mysql connection error to the czar, which should trigger a re-try.
        // _initConnection should have added an error message to _multiError.
        _initMsgs();
        _transmit(true, 0, 0); // no rows, no bytes in rows.
        return false;
    }

    if (_task->msg->has_protocol()) {
        switch(_task->msg->protocol()) {
        case 2:
            return _dispatchChannel(); // Run the query and send the results back.
        case 1:
            throw UnsupportedError(_task->getIdStr() + " QueryRunner: Expected protocol > 1 in TaskMsg");
        default:
            throw UnsupportedError(_task->getIdStr() + " QueryRunner: Invalid protocol in TaskMsg");
        }
    } else {
        throw UnsupportedError(_task->getIdStr() + " QueryRunner: Expected protocol > 1 in TaskMsg");
    }
    LOGS(_log, LOG_LVL_DEBUG, _task->getIdStr() << " QueryRunner::runQuery() END");
    return false;
}

MYSQL_RES* QueryRunner::_primeResult(std::string const& query) {
        bool queryOk = _mysqlConn->queryUnbuffered(query);
        if (!queryOk) {
            LOGS(_log, LOG_LVL_ERROR, "primeResult " << _mysqlConn->getError());
            util::Error error(_mysqlConn->getErrno(), _mysqlConn->getError());
            _multiError.push_back(error);
            return nullptr;
        }
        return _mysqlConn->getResult();
}

void QueryRunner::_initMsgs() {
    _protoHeader = std::make_shared<proto::ProtoHeader>();
    _initMsg();
}

void QueryRunner::_initMsg() {
    _result = std::make_shared<proto::Result>();
    _result->mutable_rowschema();
    _result->set_continues(0);
    if (_task->msg->has_session()) {
        _result->set_session(_task->msg->session());
    }
}

void QueryRunner::_fillSchema(MYSQL_RES* result) {
    // Build schema obj from result
    auto s = mysql::SchemaFactory::newFromResult(result);
    // Fill _result's schema from Schema obj
    for(auto i=s.columns.begin(), e=s.columns.end(); i != e; ++i) {
        proto::ColumnSchema* cs = _result->mutable_rowschema()->add_columnschema();
        cs->set_name(i->name);
        cs->set_deprecated_hasdefault(false); // still need to set deprecated but 'required' protobuf field
        cs->set_sqltype(i->colType.sqlType);
        cs->set_mysqltype(i->colType.mysqlType);
    }
}

/// Fill one row in the Result msg from one row in MYSQL_RES*
/// If the message has gotten larger than the desired message size,
/// it will be transmitted with a flag set indicating the result
/// continues in later messages.
bool QueryRunner::_fillRows(MYSQL_RES* result, int numFields, uint& rowCount, size_t& tSize) {
    MYSQL_ROW row;

    while ((row = mysql_fetch_row(result))) {
        auto lengths = mysql_fetch_lengths(result);
        proto::RowBundle* rawRow =_result->add_row();
        for(int i=0; i < numFields; ++i) {
            if (row[i]) {
                rawRow->add_column(row[i], lengths[i]);
                rawRow->add_isnull(false);
            } else {
                rawRow->add_column();
                rawRow->add_isnull(true);
            }
        }
        tSize += rawRow->ByteSize();
        ++rowCount;

        unsigned int szLimit = std::min(proto::ProtoHeaderWrap::PROTOBUFFER_DESIRED_LIMIT,
                                        proto::ProtoHeaderWrap::PROTOBUFFER_HARD_LIMIT);

        // Each element needs to be mysql-sanitized
        if (tSize > szLimit) {
            if (tSize > proto::ProtoHeaderWrap::PROTOBUFFER_HARD_LIMIT) {
                LOGS_ERROR("Message single row too large to send using protobuffer");
                return false;
            }
            LOGS(_log, LOG_LVL_DEBUG, _task->getIdStr() << " Large message size=" << tSize
                 << ", splitting message rowCount=" << rowCount);
            _transmit(false, rowCount, tSize);
            rowCount = 0;
            tSize = 0;
            _initMsg();
            // This task is going to have multiple results to return to the czar and
            // the speed this task can be completed will be limited by the czar's ability to
            // read in results, which could be very very slow. The upshot of this is the
            // scheduler for this worker should stop waiting for this task. leavePool()
            // will tell the scheduler this task is finished and create a new thread in the pool
            // to replace this thread.
            auto pet = _task->getAndNullPoolEventThread();
            if (pet != nullptr) {
                pet->leavePool();
            } else {
                LOGS(_log, LOG_LVL_DEBUG, "Large result PoolEventThread was null. Probably already moved. b");
            }
        }
    }
    return true;
}


util::TimerHistogram transmitHisto("transmit Hist", {0.1, 1, 5, 10, 20, 40});


/// Transmit result data with its header.
/// If 'last' is true, this is the last message in the result set
/// and flags are set accordingly.
void QueryRunner::_transmit(bool last, uint rowCount, size_t tSize) {
    LOGS(_log, LOG_LVL_DEBUG, _task->getIdStr() << " _transmit last=" << last
         << " rowCount=" << rowCount << " tSize=" << tSize);
    std::string resultString;
    _result->set_queryid(_task->getQueryId());
    _result->set_jobid(_task->getJobId());
    _result->set_continues(!last);
    _result->set_largeresult(_largeResult);
    _result->set_rowcount(rowCount);
    _result->set_transmitsize(tSize);
    _result->set_attemptcount(_task->getAttemptCount());
    if (!_multiError.empty()) {
        std::string chunkId = std::to_string(_task->msg->chunkid());
        std::string msg = "Error(s) in result for chunk #" + chunkId + ": " + _multiError.toOneLineString();
        _result->set_errormsg(msg);
        LOGS(_log, LOG_LVL_ERROR, msg);
    }
    _result->SerializeToString(&resultString);
    _result.reset(); // don't need it anymore and a new one will be made when needed..

    _transmitHeader(resultString);
    LOGS(_log, LOG_LVL_DEBUG, "_transmit last=" << last << " " << _task->getIdStr()
         << " resultString=" << util::prettyCharList(resultString, 5));

    if (!_cancelled) {
        // StreamBuffer::create invalidates resultString by using std::move()
        xrdsvc::StreamBuffer::Ptr streamBuf(xrdsvc::StreamBuffer::createWithMove(resultString));
        _sendBuf(streamBuf, transmitHisto, "body");
    } else {
        LOGS(_log, LOG_LVL_DEBUG, "_transmit cancelled");
    }
    _largeResult = true; // Transmits after the first are considered large results.
}


void QueryRunner::_sendBuf(xrdsvc::StreamBuffer::Ptr& streamBuf,
                           util::TimerHistogram& histo, std::string const& note) {
    bool sent = _task->sendChannel->sendStream(streamBuf, false);
    if (!sent) {
        LOGS(_log, LOG_LVL_ERROR, _task->getIdStr() << " Failed to transmit " << note << "!");
        _cancelled = true;
    } else {
        util::Timer t;
        t.start();
        streamBuf->waitForDoneWithThis(); // Block until this buffer has been sent.
        t.stop();
        auto logMsg = histo.addTime(t.getElapsed(), _task->getIdStr());
        LOGS(_log, LOG_LVL_DEBUG, logMsg);
    }
}


util::TimerHistogram transHeaderHisto("transHeader Hist", {0.1, 1, 5, 10, 20, 40});


/// Transmit the protoHeader
void QueryRunner::_transmitHeader(std::string& msg) {
    LOGS(_log, LOG_LVL_DEBUG, "_transmitHeader");
    // Set header
    _protoHeader->set_protocol(2); // protocol 2: row-by-row message
    _protoHeader->set_size(msg.size());
    _protoHeader->set_md5(util::StringHash::getMd5(msg.data(), msg.size()));
    _protoHeader->set_wname(getHostname());
    _protoHeader->set_largeresult(_largeResult);
    std::string protoHeaderString;
    _protoHeader->SerializeToString(&protoHeaderString);

    // Flush to channel.
    // Make sure protoheader size can be encoded in a byte.
    assert(protoHeaderString.size() < 255);
    if (!_cancelled) {
        auto msgBuf = proto::ProtoHeaderWrap::wrap(protoHeaderString);
        xrdsvc::StreamBuffer::Ptr streamBuf(xrdsvc::StreamBuffer::createWithMove(msgBuf)); // invalidates msgBuf
        _sendBuf(streamBuf, transHeaderHisto, "header");
    } else {
        LOGS(_log, LOG_LVL_DEBUG, _task->getIdStr() << " _transmitHeader cancelled");
    }
}

class ChunkResourceRequest {
public:
    ChunkResourceRequest(std::shared_ptr<ChunkResourceMgr> const& mgr,
                         proto::TaskMsg const& msg)
        // Use old-school member initializers because gcc 4.8.5
        // miscompiles the code when using brace initializers (DM-4704).
        : _mgr(mgr), _msg(msg) {}

    ChunkResource getResourceFragment(int i) {
        proto::TaskMsg_Fragment const& fragment(_msg.fragment(i));
        LOGS(_log, LOG_LVL_DEBUG, "fragment i=" << i);
        if (!fragment.has_subchunks()) {
            DbTableSet dbTbls;
            for (auto const& scanTbl : _msg.scantable()) {
                dbTbls.emplace(scanTbl.db(), scanTbl.table());
            }
            assert(_msg.has_db());
            LOGS(_log, LOG_LVL_DEBUG, "fragment a db=" << _msg.db() << ":" << _msg.chunkid()
                    << " dbTbls=" << util::printable(dbTbls));
            return _mgr->acquire(_msg.db(), _msg.chunkid(), dbTbls);
        }

        std::string db;
        proto::TaskMsg_Subchunk const& sc = fragment.subchunks();
        DbTableSet dbTableSet;
        for (int j=0; j < sc.dbtbl_size(); j++) {
            dbTableSet.emplace(sc.dbtbl(j).db(), sc.dbtbl(j).tbl());
        }
        IntVector subchunks(sc.id().begin(), sc.id().end());
        if (sc.has_database()) {
            db = sc.database();
        } else {
            db = _msg.db();
        }
        LOGS(_log, LOG_LVL_DEBUG, "fragment b db=" << db << ":" << _msg.chunkid()
                               << " dbTableSet" << util::printable(dbTableSet)
                               << " subChunks=" << util::printable(subchunks));
        return _mgr->acquire(db, _msg.chunkid(), dbTableSet, subchunks);

    }
private:
    std::shared_ptr<ChunkResourceMgr> _mgr;
    proto::TaskMsg const& _msg;
};

bool QueryRunner::_dispatchChannel() {
    proto::TaskMsg& m = *_task->msg;
    _initMsgs();
    bool firstResult = true;
    bool erred = false;
    int numFields = -1;
    if (m.fragment_size() < 1) {
        throw Bug("QueryRunner: No fragments to execute in TaskMsg");
    }
    ChunkResourceRequest req(_chunkResourceMgr, m);

    uint rowCount = 0;
    size_t tSize = 0;

    try {
        for(int i=0; i < m.fragment_size(); ++i) {
            if (_cancelled) {
                break;
            }
            proto::TaskMsg_Fragment const& fragment(m.fragment(i));
            std::vector<std::string> queries;
            for (const std::string queryStr: fragment.query()) {
                if (fragment.has_subchunks() && false == fragment.subchunks().id().empty()) {
                    for (auto subchunkId : fragment.subchunks().id()) {
                        std::string s(queryStr);
                        boost::algorithm::replace_all(s, SUBCHUNK_TAG, std::to_string(subchunkId));
                        queries.push_back(s);
                    }
                } else {
                    queries.push_back(queryStr);
                }
            }
            ChunkResource cr(req.getResourceFragment(i));
            // Use query fragment as-is, funnel results.
            for(auto const& query : queries) {
                util::Timer sqlTimer;
                sqlTimer.start();
                MYSQL_RES* res = _primeResult(query); // This runs the SQL query.
                sqlTimer.stop();
                LOGS(_log, LOG_LVL_DEBUG, _task->getIdStr() << " fragment time=" << sqlTimer.getElapsed()
                                                            << " query=" << query);
                if (!res) {
                    erred = true;
                    continue;
                }
                if (firstResult) {
                    firstResult = false;
                    _fillSchema(res);
                    numFields = mysql_num_fields(res);
                } // TODO: may want to confirm (cheaply) that
                // successive queries have the same result schema.
                // TODO fritzm: revisit this error strategy
                // (see pull-request for DM-216)
                // Now get rows...
                if (!_fillRows(res, numFields, rowCount, tSize)) {
                    erred = true;
                }
                _mysqlConn->freeResult();
            } // Each query in a fragment
        } // Each fragment in a msg.
    } catch(sql::SqlErrorObject const& e) {
        LOGS(_log, LOG_LVL_ERROR, "dispatchChannel " << e.errMsg());
        util::Error worker_err(e.errNo(), e.errMsg());
        _multiError.push_back(worker_err);
    }
    if (!_cancelled) {
        // Send results.
        _transmit(true, rowCount, tSize);
    } else {
        erred = true;
        // Send poison error.
        LOGS(_log, LOG_LVL_ERROR, "dispatchChannel Poisoned");
        _multiError.push_back(util::Error(-1, "Poisoned."));
        // Do we need to do any cleanup?
    }
    return !erred;
}

void QueryRunner::cancel() {
    LOGS(_log, LOG_LVL_WARN, "Trying QueryRunner::cancel() call");
    _cancelled.store(true);
    if (!_mysqlConn.get()) {
        LOGS(_log, LOG_LVL_WARN, "QueryRunner::cancel() no MysqlConn");
        return;
    }
    int status = _mysqlConn->cancel();
    switch (status) {
      case -1:
          LOGS(_log, LOG_LVL_ERROR, "QueryRunner::cancel() NOP");
          break;
      case 0:
          LOGS(_log, LOG_LVL_ERROR, "QueryRunner::cancel() success");
          break;
      case 1:
          LOGS(_log, LOG_LVL_ERROR, "QueryRunner::cancel() Error connecting to kill query.");
          break;
      case 2:
          LOGS(_log, LOG_LVL_ERROR, "QueryRunner::cancel() Error processing kill query.");
          break;
      default:
          LOGS(_log, LOG_LVL_ERROR, "QueryRunner::cancel() unknown error");
          break;
    }
}

QueryRunner::~QueryRunner() {
}

}}} // namespace lsst::qserv::wdb

// Future idea: Query cache
// Pseudocode: Record query in query cache table
/*
  result = runQuery(db.get(),
  "INSERT INTO qcache.Queries "
  "(queryTime, query, db, path) "
  "VALUES (NOW(), ?, "
  "'" + dbName + "'"
  ", "
  "'" + _task->resultPath + "'"
  ")",
  script);
  if (result.size() != 0) {
  _errorNo = EIO;
  _errorDesc += result;
  return false;
  }
*/
