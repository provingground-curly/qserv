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
#ifndef LSST_QSERV_REPLICA_CORE_FINDALLREQUEST_H
#define LSST_QSERV_REPLICA_CORE_FINDALLREQUEST_H

/// FindAllRequest.h declares:
///
/// class FindAllRequest
/// (see individual class documentation for more information)

// System headers

#include <functional>   // std::function
#include <memory>       // shared_ptr
#include <string>

// Qserv headers

#include "proto/replication.pb.h"
#include "replica_core/ReplicaInfo.h"
#include "replica_core/RequestConnection.h"

// This header declarations

namespace lsst {
namespace qserv {
namespace replica_core {

/**
  * Class FindAllRequest represents known replicas lookup requests within
  * the master controller.
  */
class FindAllRequest
    :   public RequestConnection  {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<FindAllRequest> pointer;

    /// The function type for notifications on the completon of the request
    typedef std::function<void(pointer)> callback_type;

    // Default construction and copy semantics are proxibited

    FindAllRequest () = delete;
    FindAllRequest (FindAllRequest const&) = delete;
    FindAllRequest & operator= (FindAllRequest const&) = delete;

    /// Destructor
    ~FindAllRequest () final;

    // Trivial acccessors
 
    const std::string& database        () const { return _database; }
    bool               computeCheckSum () const { return _computeCheckSum; }

   /**
     * Return a refernce to a result of the completed request.
     *
     * Note that this operation will return a sensible result only if the operation
     * finishes with status FINISHED::SUCCESS
     */
    const ReplicaInfoCollection& responseData () const;

    /**
     * Create a new request with specified parameters.
     * 
     * Static factory method is needed to prevent issue with the lifespan
     * and memory management of instances created otherwise (as values or via
     * low-level pointers).
     *
     * @param serviceProvider  - a host of services for various communications
     * @param worker           - the identifier of a worker node (the one where the chunks
     *                           expected to be located)
     * @param database         - the name of a database
     * @param onFinish         - an optional callback function to be called upon a completion of the request.
     * @param priority         - a priority level of the request
     * @param computeCheckSum  - tell a worker server to compute check/control sum on each file
     * @param keepTracking     - keep tracking the request before it finishes or fails
     */
    static pointer create (ServiceProvider         &serviceProvider,
                           boost::asio::io_service &io_service,
                           const std::string       &worker,
                           const std::string       &database,
                           callback_type            onFinish,
                           int                      priority=0,
                           bool                     computeCheckSum=false,
                           bool                     keepTracking=true);

private:

    /**
     * Construct the request with the pointer to the services provider.
     */
    FindAllRequest (ServiceProvider         &serviceProvider,
                    boost::asio::io_service &io_service,
                    const std::string       &worker,
                    const std::string       &database,
                    callback_type            onFinish,
                    int                      priority=0,
                    bool                     computeCheckSum=false,
                    bool                     keepTracking=true);

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
    void requestSent (const boost::system::error_code &ec,
                      size_t                           bytes_transferred);

    /// Start receiving the response from the destination worker
    void receiveResponse ();

    /// Callback handler for the asynchronious operation
    void responseReceived (const boost::system::error_code &ec,
                           size_t                           bytes_transferred);

    /// Start the timer before attempting the previously failed
    /// or successfull (if a status check is needed) step.
    void wait ();

    /// Callback handler for the asynchronious operation
    void awaken (const boost::system::error_code &ec);

    /// Start sending the status request to the destination worker
    void sendStatus ();

    /// Callback handler for the asynchronious operation
    void statusSent (const boost::system::error_code &ec,
                     size_t                           bytes_transferred);

    /// Start receiving the status response from the destination worker
    void receiveStatus ();

    /// Callback handler for the asynchronious operation
    void statusReceived (const boost::system::error_code &ec,
                         size_t                           bytes_transferred);

    /// Process the completion of the requested operation
    void analyze (const lsst::qserv::proto::ReplicationResponseFindAll &message);

    /**
     * Notifying a party which initiated the request.
     *
     * This method implements the corresponing virtual method defined
     * bu the base class.
     */
    void notify () final;

private:

    // Parameters of the object

    std::string _database;
    bool        _computeCheckSum;

    // Registered callback to be called when the operation finishes

    callback_type _onFinish;

    /// Result of the operation
    ReplicaInfoCollection _replicaInfoCollection;
};

}}} // namespace lsst::qserv::replica_core

#endif // LSST_QSERV_REPLICA_CORE_FINDALLREQUEST_H