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
#include "replica_core/WorkerServer.h"

// System headers

#include <boost/bind.hpp>

// Qserv headers

#include "lsst/log/Log.h"
#include "replica_core/ServiceProvider.h"
#include "replica_core/WorkerProcessor.h"

namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica_core.WorkerServer");

} /// namespace

namespace lsst {
namespace qserv {
namespace replica_core {

WorkerServer::pointer
WorkerServer::create (ServiceProvider      &serviceProvider,
                      WorkerRequestFactory &requestFactory,
                      const std::string    &workerName) {

    return WorkerServer::pointer (
        new WorkerServer (
            serviceProvider,
            requestFactory,
            workerName));
}

WorkerServer::WorkerServer (ServiceProvider      &serviceProvider,
                            WorkerRequestFactory &requestFactory,
                            const std::string    &workerName)

    :   _serviceProvider {serviceProvider},
        _workerName      {workerName},
        _processor       {serviceProvider, requestFactory, workerName},
        _workerInfo      {serviceProvider.config()->workerInfo(workerName)},
        _io_service (),
        _acceptor (
            _io_service,
            boost::asio::ip::tcp::endpoint (
                boost::asio::ip::tcp::v4(),
                _workerInfo.svcPort)) {

    // Set the socket reuse option to allow recycling ports after catastrifc
    // failures.

    _acceptor.set_option(boost::asio::socket_base::reuse_address(true));
}

void
WorkerServer::run () {

    // Start the processor to allow processing requests.

    _processor.run();

    // Begin accepting connections before running the service to allow
    // asynchronous operations. Otherwise the service will finish right
    // away.

    beginAccept();

    _io_service.run();
}

void
WorkerServer::beginAccept () {

    WorkerServerConnection::pointer connection =
        WorkerServerConnection::create (
            _serviceProvider,
            _processor,
            _io_service
        );
        
    _acceptor.async_accept (
        connection->socket(),
        boost::bind (
            &WorkerServer::handleAccept,
            shared_from_this(),
            connection,
            boost::asio::placeholders::error
        )
    );
}

void
WorkerServer::handleAccept (const WorkerServerConnection::pointer &connection,
                            const boost::system::error_code       &err) {

    if (!err) {
        connection->beginProtocol();
    } else {

        // TODO: Consider throwing an exception instead. Another option
        //       would be to log the message via the standard log file
        //       mechanism since its' safe to ignore problems with
        //       incoming connections due a lack of side effects.

        LOGS(_log, LOG_LVL_DEBUG, context() << "handleAccept  err:" << err);
    }
    beginAccept();
}

}}} // namespace lsst::qserv::replica_core
