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
#ifndef LSST_QSERV_REPLICA_CORE_REPLICATIONREQUEST_H
#define LSST_QSERV_REPLICA_CORE_REPLICATIONREQUEST_H

/// ReplicationRequest.h declares:
///
/// Common classes shared by both implementations:
///
///   class ReplicationRequest
///
/// Request implementations based on individual connectors provided by
/// base class RequestConnection:
///
///   class ReplicationRequestC
///
/// Request implementations based on multiplexed connectors provided by
/// base class RequestMessenger:
///
///   class ReplicationRequestM
///
/// (see individual class documentation for more information)

// System headers

#include <functional>   // std::function
#include <memory>       // shared_ptr
#include <string>

// Qserv headers

#include "proto/replication.pb.h"
#include "replica_core/Common.h"
#include "replica_core/ReplicaInfo.h"
#include "replica_core/RequestConnection.h"
#include "replica_core/RequestMessenger.h"

// This header declarations

namespace lsst {
namespace qserv {
namespace replica_core {

// Forward declarations

class Messenger;


// =============================================
//   Classes based on the dedicated connectors
// =============================================

/**
  * Class ReplicationRequestC represents a transient state of requests
  * within the master controller for creating reolicas.
  */
class ReplicationRequestC
    :   public RequestConnection  {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<ReplicationRequestC> pointer;

    /// The function type for notifications on the completon of the request
    typedef std::function<void(pointer)> callback_type;

    // Default construction and copy semantics are proxibited

    ReplicationRequestC () = delete;
    ReplicationRequestC (ReplicationRequestC const&) = delete;
    ReplicationRequestC& operator= (ReplicationRequestC const&) = delete;

    /// Destructor
    ~ReplicationRequestC () final;

    // Trivial acccessors

    std::string const& database     () const { return _database; }
    unsigned int       chunk        () const { return _chunk; }
    std::string const& sourceWorker () const { return _sourceWorker; }

    /// Return target request specific parameters
    ReplicationRequestParams const& targetRequestParams () const { return _targetRequestParams; }

    /// Return the status of the replica
    ReplicaInfo const& responseData () const { return _replicaInfo; }

    /**
     * Create a new request with specified parameters.
     * 
     * Static factory method is needed to prevent issue with the lifespan
     * and memory management of instances created otherwise (as values or via
     * low-level pointers).
     *
     * @param serviceProvider - a host of services for various communications
     * @param io_service      - BOOST ASIO API
     * @param worker          - the identifier of a worker node (the one to be affectd by the replication)
     *                          at a destination of the chunk
     * @param sourceWorker    - the identifier of a worker node at a source of the chunk
     * @param database        - the name of a database
     * @param chunk           - the number of a chunk to replicate (implies all relevant tables)
     * @param onFinish        - an optional callback function to be called upon a completion of the request.
     * @param priority        - a priority level of the request
     * @param keepTracking    - keep tracking the request before it finishes or fails
     * @param allowDuplicate  - follow a previously made request if the current one duplicates it
     */
    static pointer create (ServiceProvider&         serviceProvider,
                           boost::asio::io_service& io_service,
                           std::string const&       worker,
                           std::string const&       sourceWorker,
                           std::string const&       database,
                           unsigned int             chunk,
                           callback_type            onFinish,
                           int                      priority,
                           bool                     keepTracking,
                           bool                     allowDuplicate);

private:

    /**
     * Construct the request with the pointer to the services provider.
     */
    ReplicationRequestC (ServiceProvider&         serviceProvider,
                         boost::asio::io_service& io_service,
                         std::string const&       worker,
                         std::string const&       sourceWorker,
                         std::string const&       database,
                         unsigned int             chunk,
                         callback_type            onFinish,
                         int                      priority,
                         bool                     keepTracking,
                         bool                     allowDuplicate);

    /**
      * This method is called when a connection is established and
      * the stack is ready to begin implementing an actual protocol
      * with the worker server.
      *
      * The first step of teh protocol will be to send the replication
      * request to the destination worker.
      */
    void beginProtocol () final;
    
    /// Callback handler for the asynchronious operation
    void requestSent (boost::system::error_code const& ec,
                      size_t                           bytes_transferred);

    /// Start receiving the response from the destination worker
    void receiveResponse ();

    /// Callback handler for the asynchronious operation
    void responseReceived (boost::system::error_code const& ec,
                           size_t                           bytes_transferred);

    /// Start the timer before attempting the previously failed
    /// or successfull (if a status check is needed) step.
    void wait ();

    /// Callback handler for the asynchronious operation
    void awaken (boost::system::error_code const& ec);

    /// Start sending the status request to the destination worker
    void sendStatus ();

    /// Callback handler for the asynchronious operation
    void statusSent (boost::system::error_code const& ec,
                     size_t                           bytes_transferred);

    /// Start receiving the status response from the destination worker
    void receiveStatus ();

    /// Callback handler for the asynchronious operation
    void statusReceived (boost::system::error_code const& ec,
                         size_t                           bytes_transferred);

    /// Process the completion of the requested operation
    void analyze (lsst::qserv::proto::ReplicationResponseReplicate const& message);

    /**
     * Notifying a party which initiated the request.
     *
     * This method implements the corresponing virtual method defined
     * bu the base class.
     */
    void notify () final;

private:

    // Parameters of the object

    std::string  _database;
    unsigned int _chunk;
    std::string  _sourceWorker;
    
    /// Registered callback to be called when the operation finishes
    callback_type _onFinish;

    /// Request-specific parameters of the target request
    ReplicationRequestParams _targetRequestParams;

    /// Detailed info on the replica status
    ReplicaInfo _replicaInfo;
};


// ===============================================
//   Classes based on the multiplexed connectors
// ===============================================

/**
  * Class ReplicationRequestM represents a transient state of requests
  * within the master controller for creating reolicas.
  */
class ReplicationRequestM
    :   public RequestMessenger  {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<ReplicationRequestM> pointer;

    /// The function type for notifications on the completon of the request
    typedef std::function<void(pointer)> callback_type;

    // Default construction and copy semantics are proxibited

    ReplicationRequestM () = delete;
    ReplicationRequestM (ReplicationRequestM const&) = delete;
    ReplicationRequestM& operator= (ReplicationRequestM const&) = delete;

    /// Destructor
    ~ReplicationRequestM () final;

    // Trivial acccessors

    std::string const& database     () const { return _database; }
    unsigned int       chunk        () const { return _chunk; }
    std::string const& sourceWorker () const { return _sourceWorker; }

    /// Return target request specific parameters
    ReplicationRequestParams const& targetRequestParams () const { return _targetRequestParams; }

    /// Return request-specific extended data reported upon a successfull completion
    /// of the request
    ReplicaInfo const& responseData () const { return _replicaInfo; }

    
    /**
     * Create a new request with specified parameters.
     * 
     * Static factory method is needed to prevent issue with the lifespan
     * and memory management of instances created otherwise (as values or via
     * low-level pointers).
     *
     * @param serviceProvider - a host of services for various communications
     * @param io_service      - BOOST ASIO API
     * @param worker          - the identifier of a worker node (the one to be affectd by the replication)
     *                          at a destination of the chunk
     * @param sourceWorker    - the identifier of a worker node at a source of the chunk
     * @param database        - the name of a database
     * @param chunk           - the number of a chunk to replicate (implies all relevant tables)
     * @param onFinish        - an optional callback function to be called upon a completion of the request.
     * @param priority        - a priority level of the request
     * @param keepTracking    - keep tracking the request before it finishes or fails
     * @param allowDuplicate  - follow a previously made request if the current one duplicates it
     * @param messenger       - worker messenging service
     */
    static pointer create (ServiceProvider&                  serviceProvider,
                           boost::asio::io_service&          io_service,
                           std::string const&                worker,
                           std::string const&                sourceWorker,
                           std::string const&                database,
                           unsigned int                      chunk,
                           callback_type                     onFinish,
                           int                               priority,
                           bool                              keepTracking,
                           bool                              allowDuplicate,
                           std::shared_ptr<Messenger> const& messenger);

private:

    /**
     * Construct the request with the pointer to the services provider.
     */
    ReplicationRequestM (ServiceProvider&                  serviceProvider,
                         boost::asio::io_service&          io_service,
                         std::string const&                worker,
                         std::string const&                sourceWorker,
                         std::string const&                database,
                         unsigned int                      chunk,
                         callback_type                     onFinish,
                         int                               priority,
                         bool                              keepTracking,
                         bool                              allowDuplicate,
                         std::shared_ptr<Messenger> const& messenger);

    /**
      * Implement the method declared in the base class
      *
      * @see Request::startImpl()
      */
    void startImpl () final;

    /// Start the timer before attempting the previously failed
    /// or successfull (if a status check is needed) step.
    void wait ();

    /// Callback handler for the asynchronious operation
    void awaken (boost::system::error_code const& ec);

    /// Send the serialized content of the buffer to a worker
    void send ();

    /// Process the completion of the requested operation
    void analyze (bool                                                    success,
                  lsst::qserv::proto::ReplicationResponseReplicate const& message);

    /**
     * Notifying a party which initiated the request.
     *
     * This method implements the corresponing virtual method defined
     * bu the base class.
     */
    void notify () final;

private:

    // Parameters of the object

    std::string  _database;
    unsigned int _chunk;
    std::string  _sourceWorker;
    
    /// Registered callback to be called when the operation finishes
    callback_type _onFinish;

    /// Request-specific parameters of the target request
    ReplicationRequestParams _targetRequestParams;

    /// Detailed info on the replica status
    ReplicaInfo _replicaInfo;
};


// =================================================================
//   Type switch as per the macro defined in replica_core/Common.h
// =================================================================

#ifdef LSST_QSERV_REPLICA_CORE_REQUEST_BASE_C
typedef ReplicationRequestC ReplicationRequest;
#else
typedef ReplicationRequestM ReplicationRequest;
#endif // LSST_QSERV_REPLICA_CORE_REQUEST_BASE_C

}}} // namespace lsst::qserv::replica_core

#endif // LSST_QSERV_REPLICA_CORE_REPLICATIONREQUEST_H