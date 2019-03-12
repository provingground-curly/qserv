/*
 * LSST Data Management System
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
#ifndef LSST_QSERV_HTTPPROCESSOR_H
#define LSST_QSERV_HTTPPROCESSOR_H

// System headers
#include <functional>
#include <memory>
#include <set>

// Third party headers
#include "nlohmann/json.hpp"

// Qserv headers
#include "qhttp/Server.h"
#include "replica/DeleteWorkerTask.h"
#include "replica/HealthMonitorTask.h"
#include "replica/ReplicationTask.h"
#include "util/Mutex.h"

// LSST headers
#include "lsst/log/Log.h"

// This header declarations

namespace lsst {
namespace qserv {
namespace replica {

/**
 * Class HttpProcessor processes requests from the built-in HTTP server.
 * The constructor of the class will register requests handlers an start
 * the server.
 */
class HttpProcessor : public std::enable_shared_from_this<HttpProcessor> {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<HttpProcessor> Ptr;

    // Default construction and copy semantics are prohibited

    HttpProcessor() = delete;
    HttpProcessor(HttpProcessor const&) = delete;
    HttpProcessor& operator=(HttpProcessor const&) = delete;

    /// The non-trivial destructor is needed to shut down the HTTP server
    ~HttpProcessor();

    /**
     * Create a new object with specified parameters.
     *
     * Static factory method is needed to prevent issue with the lifespan
     * and memory management of instances created otherwise (as values or via
     * low-level pointers).
     *
     * @param controller
     *   a reference to the Controller for launching requests, jobs, etc.
     *
     * @param onTerminated
     *   callback function to be called upon abnormal termination
     *   of the task. Set it to 'nullptr' if no call back should be made.
     *
     * @param onWorkerEvict
     *   callback function to be called when one or more workers
     *   were requested to be explicitly evicted from the cluster.
     *
     * @param healthMonitorTask
     *   a reference to the Cluster Health Monitoring task is made visible
     *   in a context of this lass to allow suspending/resuming it as needed.
     *
     * @param replicationTask
     *   a reference to the Linear Replication Loop task is made visible
     *   in a context of this lass to allow suspending/resuming it as needed.
     *
     * @param deleteWorkerTask
     *   a reference to the Worker Eviction task is made visible
     *   in a context of this lass to allow suspending/resuming it as needed.
     *
     * @return
     *   the smart pointer to a new object
     */
    static Ptr create(Controller::Ptr const& controller,
                      HealthMonitorTask::WorkerEvictCallbackType const& onWorkerEvict,
                      HealthMonitorTask::Ptr const& healthMonitorTask,
                      ReplicationTask::Ptr const& replicationTask,
                      DeleteWorkerTask::Ptr const& deleteWorkerTask);

    
    /// @return reference to the Replication Framework's Controller
    Controller::Ptr const controller() const { return _controller; }

private:

    /**
     * The constructor is available to the class's factory method
     *
     * @see HttpProcessor::create()
     */
    HttpProcessor(Controller::Ptr const& controller,
                  HealthMonitorTask::WorkerEvictCallbackType const& onWorkerEvict,
                  HealthMonitorTask::Ptr const& healthMonitorTask,
                  ReplicationTask::Ptr const& replicationTask,
                  DeleteWorkerTask::Ptr const& deleteWorkerTask);


    /**
     * Delayed initialization.
     */
    void _initialize();

    /**
     * @return the context string to be used when logging messages into
     * a log stream.
     */
    std::string _context() const;

    /**
     * Log a message into the Logger's LOG_LVL_INFO stream
     *
     * @param msg
     *   a message to be logged
     */
    void _info(std::string const& msg);

    /**
     * Log a message into the Logger's LOG_LVL_DEBUG stream
     *
     * @param msg
     *   a message to be logged
     */
    void _debug(std::string const& msg);

    /**
     * Log a message into the Logger's LOG_LVL_ERROR stream
     *
     * @param msg
     *   a message to be logged
     */
    void _error(std::string const& msg);

    // --------------------------------
    // Callback for processing requests
    // --------------------------------

    /**
     * Process a request which return status of one worker.
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _getWorkerStatus(qhttp::Request::Ptr req,
                          qhttp::Response::Ptr resp);

    /**
     * Process a request which return the status of the replicas.
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _getReplicationLevel(qhttp::Request::Ptr req,
                              qhttp::Response::Ptr resp);

    /**
     * Process a request which return status of all workers.
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _listWorkerStatuses(qhttp::Request::Ptr req,
                             qhttp::Response::Ptr resp);

    /**
     * Process a request which return info on known Replication Controllers
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _listControllers(qhttp::Request::Ptr req,
                          qhttp::Response::Ptr resp);

    /**
     * Process a request which return info on the specified Replication Controller
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _getControllerInfo(qhttp::Request::Ptr req,
                            qhttp::Response::Ptr resp);

    /**
     * Process a request which return info on known Replication Requests
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _listRequests(qhttp::Request::Ptr req,
                       qhttp::Response::Ptr resp);

    /**
     * Process a request which return info on the specified Replication Request
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _getRequestInfo(qhttp::Request::Ptr req,
                         qhttp::Response::Ptr resp);

    /**
     * Process a request which return info on known Replication Jobs
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _listJobs(qhttp::Request::Ptr req,
                   qhttp::Response::Ptr resp);

    /**
     * Process a request which return info on the specified Replication Job
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _getJobInfo(qhttp::Request::Ptr req,
                     qhttp::Response::Ptr resp);

    /**
     * Process a request which return the Configuration of the Replication system
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _getConfig(qhttp::Request::Ptr req,
                    qhttp::Response::Ptr resp);

    /**
     * Process a request which updates the Configuration of the Replication system
     * and reports back its new state.
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _updateGeneralConfig(qhttp::Request::Ptr req,
                              qhttp::Response::Ptr resp);

    /**
     * Process a request which updates parameters of an existing worker in the Configuration
     * of the Replication system and reports back the new state of the system
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _updateWorkerConfig(qhttp::Request::Ptr req,
                             qhttp::Response::Ptr resp);

    /**
     * Process a request which removes an existing worker from the Configuration
     * of the Replication system and reports back the new state of the system
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _deleteWorkerConfig(qhttp::Request::Ptr req,
                             qhttp::Response::Ptr resp);

    /**
     * Process a request which adds a new worker into the Configuration
     * of the Replication system and reports back the new state of the system
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _addWorkerConfig(qhttp::Request::Ptr req,
                          qhttp::Response::Ptr resp);

    /**
     * Process a request which removes an existing database family from the Configuration
     * of the Replication system and reports back the new state of the system
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _deleteFamilyConfig(qhttp::Request::Ptr req,
                             qhttp::Response::Ptr resp);

    /**
     * Process a request which adds a new database family into the Configuration
     * of the Replication system and reports back the new state of the system
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _addFamilyConfig(qhttp::Request::Ptr req,
                          qhttp::Response::Ptr resp);

    /**
     * Process a request which removes an existing database from the Configuration
     * of the Replication system and reports back the new state of the system
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _deleteDatabaseConfig(qhttp::Request::Ptr req,
                               qhttp::Response::Ptr resp);

    /**
     * Process a request which adds a new database into the Configuration
     * of the Replication system and reports back the new state of the system
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _addDatabaseConfig(qhttp::Request::Ptr req,
                            qhttp::Response::Ptr resp);

    /**
     * Process a request which removes an existing table from the Configuration
     * of the Replication system and reports back the new state of the system
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _deleteTableConfig(qhttp::Request::Ptr req,
                            qhttp::Response::Ptr resp);

    /**
     * Process a request which adds a new database table into the Configuration
     * of the Replication system and reports back the new state of the system
     *
     * @param req   request received from a client
     * @param resp  response to be sent back
     */
    void _addTableConfig(qhttp::Request::Ptr req,
                        qhttp::Response::Ptr resp);

    /**
     * Pull the current Configuration and translate it into a JSON object
     * 
     * @return JSON object
     */
    nlohmann::json _configToJson() const;

private:

    /// The reference to the Replication Framework's Controller
    Controller::Ptr const _controller;

    /// The callback to be called when there is a request to evict one
    // or many workers from the cluster.
    HealthMonitorTask::WorkerEvictCallbackType const _onWorkerEvict;

    // References(!) to smart pointers to the tasks which can be managed
    // by this class.
    //
    // @note
    //   References to the pointers are used to avoid increasing the reference
    //   counters to the objects.

    HealthMonitorTask::Ptr const& _healthMonitorTask;
    ReplicationTask::Ptr   const& _replicationTask;
    DeleteWorkerTask::Ptr  const& _deleteWorkerTask;

    /// The cache for the latest state of the replication levels report
    std::string _replicationLevelReport;
    
    /// The timestamp for when the last report was made
    uint64_t _replicationLevelReportTimeMs;

    /// Mutex guarding the cache with the replication levels report
    util::Mutex _replicationLevelMtx;

    /// Message logger
    LOG_LOGGER _log;

};
    
}}} // namespace lsst::qserv::replica

#endif // LSST_QSERV_HTTPPROCESSOR_H
