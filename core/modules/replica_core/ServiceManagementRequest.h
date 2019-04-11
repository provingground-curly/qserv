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
#ifndef LSST_QSERV_REPLICA_CORE_SERVICEMANAGEMENTREQUEST_H
#define LSST_QSERV_REPLICA_CORE_SERVICEMANAGEMENTREQUEST_H

/// ServiceManagementRequest.h declares:
///
/// Common classes shared by both implementations:
///
///   class ServiceState
///   class ServiceSuspendRequestPolicy
///   class ServiceManagementRequestBase
///   class ServiceManagementRequest
///   class ServiceSuspendRequest
///   class ServiceResumeRequest
///   class ServiceStatusRequest
///
/// Request implementations based on individual connectors provided by
/// base class RequestConnection:
///
///   class ServiceManagementRequestBaseC
///   class ServiceManagementRequestC
///   class ServiceSuspendRequestC
///   class ServiceResumeRequestC
///   class ServiceStatusRequestC
///
/// Request implementations based on multiplexed connectors provided by
/// base class RequestMessenger:
///
///   class ServiceManagementRequestBaseM
///   class ServiceManagementRequestM
///   class ServiceSuspendRequestM
///   class ServiceResumeRequestM
///   class ServiceStatusRequestM
///
/// (see individual class documentation for more information)

// System headers

#include <functional>   // std::function
#include <memory>       // shared_ptr
#include <ostream>
#include <string>
#include <vector>

// Qserv headers

#include "proto/replication.pb.h"
#include "replica_core/Common.h"
#include "replica_core/RequestConnection.h"
#include "replica_core/RequestMessenger.h"

// This header declarations

namespace lsst {
namespace qserv {
namespace replica_core {

// Forward declarations

class Messenger;

/**
 * This structure encapsulates various parameters representing the state
 * of the remote request processing service. The parameters are available
 * upon the completion of the request.
 */
struct ServiceState {

    // Its state
    enum State {
        SUSPEND_IN_PROGRESS = 0,
        SUSPENDED           = 1,
        RUNNING             = 2
    };
    State state;

    /// Return string representation of the state
    std::string state2string () const {
        switch (state) {
            case SUSPEND_IN_PROGRESS: return "SUSPEND_IN_PROGRESS";
            case SUSPENDED:           return "SUSPENDED";
            case RUNNING:             return "RUNNING";
        }
        return "";
    }

    /// The backend technology
    std::string technology;

    /// When the service started (milliseconds since UNIX Epoch)
    uint64_t startTime;

    // Counters for requests known to the service since its last start

    uint32_t numNewRequests;
    uint32_t numInProgressRequests;
    uint32_t numFinishedRequests;
    
    std::vector<lsst::qserv::proto::ReplicationServiceResponseInfo> newRequests;
    std::vector<lsst::qserv::proto::ReplicationServiceResponseInfo> inProgressRequests;
    std::vector<lsst::qserv::proto::ReplicationServiceResponseInfo> finishedRequests;
    
    /// Set parameter values from a protobuf object
    void set (const lsst::qserv::proto::ReplicationServiceResponse &message);
};

/// Overloaded streaming operator for type ServiceState
std::ostream& operator<< (std::ostream &os, const ServiceState &ss);


// ========================================================================
//   Customizations for specific request types require dedicated policies
// ========================================================================

struct ServiceSuspendRequestPolicy {
    static const char* requestTypeName () {
        return "SERVICE_SUSPEND";
    } 
    static lsst::qserv::proto::ReplicationServiceRequestType requestType () {
        return lsst::qserv::proto::ReplicationServiceRequestType::SERVICE_SUSPEND;
    }
};
struct ServiceResumeRequestPolicy {
    static const char* requestTypeName () {
        return "SERVICE_RESUME";
    }   
    static lsst::qserv::proto::ReplicationServiceRequestType requestType () {
        return lsst::qserv::proto::ReplicationServiceRequestType::SERVICE_RESUME;
    }
};
struct ServiceStatusRequestPolicy {
    static const char* requestTypeName () {
        return "SERVICE_STATUS";
    } 
    static lsst::qserv::proto::ReplicationServiceRequestType requestType () {
        return lsst::qserv::proto::ReplicationServiceRequestType::SERVICE_STATUS;
    }
};
struct ServiceRequestsRequestPolicy {
    static const char* requestTypeName () {
        return "SERVICE_REQUESTS";
    } 
    static lsst::qserv::proto::ReplicationServiceRequestType requestType () {
        return lsst::qserv::proto::ReplicationServiceRequestType::SERVICE_REQUESTS;
    }
};
struct ServiceDrainRequestPolicy {
    static const char* requestTypeName () {
        return "SERVICE_DRAIN";
    } 
    static lsst::qserv::proto::ReplicationServiceRequestType requestType () {
        return lsst::qserv::proto::ReplicationServiceRequestType::SERVICE_DRAIN;
    }
};

// =============================================
//   Classes based on the dedicated connectors
// =============================================

/**
  * Class ServiceManagementRequestBase is the base class for a family of requests
  * managing worker-side replication service. The only variable parameter of this
  * class is a specific type of the managemenyt request.
  *
  * Note that this class can't be instantiate directly. It serves as an implementation
  * of the protocol. All final customizations and type-specific operations are
  * provided via a generic subclass.
  */
class ServiceManagementRequestBaseC
    :   public RequestConnection {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<ServiceManagementRequestBaseC> pointer;

    // Default construction and copy semantics are proxibited

    ServiceManagementRequestBaseC () = delete;
    ServiceManagementRequestBaseC (ServiceManagementRequestBaseC const&) = delete;
    ServiceManagementRequestBaseC& operator= (ServiceManagementRequestBaseC const&) = delete;

    /// Destructor
    virtual ~ServiceManagementRequestBaseC ();

    /**
     * Get the state of the worker-side service
     *
     * This method will throw exception std::logic_error if the request's primary state
     * is not 'FINISHED' and its extended state is neither 'SUCCESS" or 'SERVER_ERROR'.
     */
    const ServiceState& getServiceState () const;

protected:

    /**
     * Construct the request with the pointer to the services provider.
     */
    ServiceManagementRequestBaseC (ServiceProvider&                                 serviceProvider,
                                  boost::asio::io_service&                          io_service,
                                  char const*                                       requestTypeName,
                                  std::string const&                                worker,
                                  lsst::qserv::proto::ReplicationServiceRequestType requestType);
private:

    /**
      * This method is called when a connection  with the worker server is established
      * and the communication stack is ready to begin implementing the actual protocol.
      *
      * The first step in the protocol will be to send the replication
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

    /// Process the worker response to the requested operation
    void analyze (const lsst::qserv::proto::ReplicationServiceResponse &message);

private:

    /// Request type
    lsst::qserv::proto::ReplicationServiceRequestType _requestType;

    /// Detailed status of the worker-side service obtained upon completion of
    /// the management request.
    ServiceState _serviceState;
};


/**
  * Generic class ServiceManagementRequest extends its base class
  * to allow further policy-based customization of specific requests.
  */
template <typename POLICY>
class ServiceManagementRequestC
    :   public ServiceManagementRequestBaseC {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<ServiceManagementRequestC<POLICY>> pointer;

    /// The function type for notifications on the completon of the request
    typedef std::function<void(pointer)> callback_type;

    // Default construction and copy semantics are proxibited

    ServiceManagementRequestC () = delete;
    ServiceManagementRequestC (ServiceManagementRequestC const&) = delete;
    ServiceManagementRequestC& operator= (ServiceManagementRequestC const&) = delete;

    /// Destructor
    ~ServiceManagementRequestC () final {
    }

    /**
     * Create a new request with specified parameters.
     * 
     * Static factory method is needed to prevent issue with the lifespan
     * and memory management of instances created otherwise (as values or via
     * low-level pointers).
     *
     * @param serviceProvider  - a host of services for various communications
     * @param worker           - the identifier of a worker node (the one to be affectd by the request)
     * @param io_service       - network communication service
     * @param onFinish         - an optional callback function to be called upon a completion of
     *                           the request.
     */
    static pointer create (ServiceProvider&         serviceProvider,
                           boost::asio::io_service& io_service,
                           std::string const&       worker,
                           callback_type            onFinish) {

        return ServiceManagementRequestC<POLICY>::pointer (
            new ServiceManagementRequestC<POLICY> (
                serviceProvider,
                io_service,
                POLICY::requestTypeName(),
                worker,
                POLICY::requestType(),
                onFinish));
    }

private:

    /**
     * Construct the request
     */
    ServiceManagementRequestC (ServiceProvider&                                  serviceProvider,
                               boost::asio::io_service&                          io_service,
                               char const*                                       requestTypeName,
                               std::string const&                                worker,
                               lsst::qserv::proto::ReplicationServiceRequestType requestType,
                               callback_type                                     onFinish)

        :   ServiceManagementRequestBaseC (serviceProvider,
                                           io_service,
                                           requestTypeName,
                                           worker,
                                           requestType),
            _onFinish (onFinish)
    {}

    /**
     * Notifying a party which initiated the request.
     *
     * This method implements the corresponing virtual method defined
     * bu the base class.
     */
    void notify () final {
        if (_onFinish != nullptr) {
            ServiceManagementRequestC<POLICY>::pointer self = shared_from_base<ServiceManagementRequestC<POLICY>>();
            _onFinish(self);
        }
    }

private:

    /// Registered callback to be called when the operation finishes
    callback_type _onFinish;
};


// ===============================================
//   Classes based on the multiplexed connectors
// ===============================================

/**
  * Class ServiceManagementRequestBaseM is the base class for a family of requests
  * managing worker-side replication service. The only variable parameter of this
  * class is a specific type of the managemenyt request.
  *
  * Note that this class can't be instantiate directly. It serves as an implementation
  * of the protocol. All final customizations and type-specific operations are
  * provided via a generic subclass.
  */
class ServiceManagementRequestBaseM
    :   public RequestMessenger {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<ServiceManagementRequestBaseM> pointer;

    // Default construction and copy semantics are proxibited

    ServiceManagementRequestBaseM () = delete;
    ServiceManagementRequestBaseM (ServiceManagementRequestBaseM const&) = delete;
    ServiceManagementRequestBaseM& operator= (ServiceManagementRequestBaseM const&) = delete;

    /// Destructor
    virtual ~ServiceManagementRequestBaseM ();

    /**
     * Get the state of the worker-side service
     *
     * This method will throw exception std::logic_error if the request's primary state
     * is not 'FINISHED' and its extended state is neither 'SUCCESS" or 'SERVER_ERROR'.
     */
    const ServiceState& getServiceState () const;

protected:

    /**
     * Construct the request with the pointer to the services provider.
     */
    ServiceManagementRequestBaseM (ServiceProvider&                                  serviceProvider,
                                   boost::asio::io_service&                          io_service,
                                   char const*                                       requestTypeName,
                                   std::string const&                                worker,
                                   lsst::qserv::proto::ReplicationServiceRequestType requestType,
                                   std::shared_ptr<Messenger> const&                 messenger);
private:

    /**
      * Implement the method declared in the base class
      *
      * @see Request::startImpl()
      */
    void startImpl () final;

    /**
     * Process the worker response to the requested operation.
     *
     * @param success - the flag indicating if the operation was successfull
     * @param message - a response from the worker service (if success is 'true')
     */
    void analyze (bool                                                  success,
                  lsst::qserv::proto::ReplicationServiceResponse const& message);

private:

    /// Request type
    lsst::qserv::proto::ReplicationServiceRequestType _requestType;

    /// Detailed status of the worker-side service obtained upon completion of
    /// the management request.
    ServiceState _serviceState;
};


/**
  * Generic class ServiceManagementRequestM extends its base class
  * to allow further policy-based customization of specific requests.
  */
template <typename POLICY>
class ServiceManagementRequestM
    :   public ServiceManagementRequestBaseM {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<ServiceManagementRequestM<POLICY>> pointer;

    /// The function type for notifications on the completon of the request
    typedef std::function<void(pointer)> callback_type;

    // Default construction and copy semantics are proxibited

    ServiceManagementRequestM () = delete;
    ServiceManagementRequestM (ServiceManagementRequestM const&) = delete;
    ServiceManagementRequestM& operator= (ServiceManagementRequestM const&) = delete;

    /// Destructor
    ~ServiceManagementRequestM () final {
    }

    /**
     * Create a new request with specified parameters.
     * 
     * Static factory method is needed to prevent issue with the lifespan
     * and memory management of instances created otherwise (as values or via
     * low-level pointers).
     *
     * @param serviceProvider  - a host of services for various communications
     * @param worker           - the identifier of a worker node (the one to be affectd by the request)
     * @param io_service       - network communication service
     * @param onFinish         - an optional callback function to be called upon a completion of
     *                           the request.
     * @param messenger       - an interface for communicating with workers
     */
    static pointer create (ServiceProvider&                  serviceProvider,
                           boost::asio::io_service&          io_service,
                           std::string const&                worker,
                           callback_type                     onFinish,
                           std::shared_ptr<Messenger> const& messenger) {

        return ServiceManagementRequestM<POLICY>::pointer (
            new ServiceManagementRequestM<POLICY> (
                serviceProvider,
                io_service,
                POLICY::requestTypeName(),
                worker,
                POLICY::requestType(),
                onFinish,
                messenger));
    }

private:

    /**
     * Construct the request
     */
    ServiceManagementRequestM (ServiceProvider&                                  serviceProvider,
                               boost::asio::io_service&                          io_service,
                               char const*                                       requestTypeName,
                               std::string const&                                worker,
                               lsst::qserv::proto::ReplicationServiceRequestType requestType,
                               callback_type                                     onFinish,
                               std::shared_ptr<Messenger> const&                 messenger)

        :   ServiceManagementRequestBaseM (serviceProvider,
                                           io_service,
                                           requestTypeName,
                                           worker,
                                           requestType,
                                           messenger),
            _onFinish (onFinish)
    {}

    /**
     * Notifying a party which initiated the request.
     *
     * This method implements the corresponing virtual method defined
     * bu the base class.
     */
    void notify () final {
        if (_onFinish != nullptr) {
            ServiceManagementRequestM<POLICY>::pointer self = shared_from_base<ServiceManagementRequestM<POLICY>>();
            _onFinish(self);
        }
    }

private:

    /// Registered callback to be called when the operation finishes
    callback_type _onFinish;
};


// =================================================================
//   Type switch as per the macro defined in replica_core/Common.h
// =================================================================

#ifdef LSST_QSERV_REPLICA_CORE_REQUEST_BASE_C

typedef ServiceManagementRequestBaseC ServiceManagementRequestBase;

typedef ServiceManagementRequestC<ServiceSuspendRequestPolicy>  ServiceSuspendRequest;
typedef ServiceManagementRequestC<ServiceResumeRequestPolicy>   ServiceResumeRequest;
typedef ServiceManagementRequestC<ServiceStatusRequestPolicy>   ServiceStatusRequest;
typedef ServiceManagementRequestC<ServiceRequestsRequestPolicy> ServiceRequestsRequest;
typedef ServiceManagementRequestC<ServiceDrainRequestPolicy>    ServiceDrainRequest;

#else  // LSST_QSERV_REPLICA_CORE_REQUEST_BASE_C

typedef ServiceManagementRequestBaseM ServiceManagementRequestBase;

typedef ServiceManagementRequestM<ServiceSuspendRequestPolicy>  ServiceSuspendRequest;
typedef ServiceManagementRequestM<ServiceResumeRequestPolicy>   ServiceResumeRequest;
typedef ServiceManagementRequestM<ServiceStatusRequestPolicy>   ServiceStatusRequest;
typedef ServiceManagementRequestM<ServiceRequestsRequestPolicy> ServiceRequestsRequest;
typedef ServiceManagementRequestM<ServiceDrainRequestPolicy>    ServiceDrainRequest;

#endif // LSST_QSERV_REPLICA_CORE_REQUEST_BASE_C

}}} // namespace lsst::qserv::replica_core

#endif // LSST_QSERV_REPLICA_CORE_SERVICEMANAGEMENTREQUEST_H