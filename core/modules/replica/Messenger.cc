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

// Class header
#include "replica/Messenger.h"

// System headers
#include <stdexcept>

// Qserv headers
#include "lsst/log/Log.h"
#include "replica/Configuration.h"
#include "replica/ServiceProvider.h"

using namespace std;

namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica.Messenger");

} /// namespace

namespace lsst {
namespace qserv {
namespace replica {

Messenger::Ptr Messenger::create(ServiceProvider::Ptr const& serviceProvider,
                                 boost::asio::io_service& io_service) {
    return Messenger::Ptr(
        new Messenger(serviceProvider,
                      io_service));
}


Messenger::Messenger(ServiceProvider::Ptr const& serviceProvider,
                     boost::asio::io_service& io_service) {

    for (auto&& worker: serviceProvider->config()->allWorkers()) {
        _workerConnector[worker] = MessengerConnector::create(serviceProvider,
                                                              io_service,
                                                              worker);
    }
}


void Messenger::stop() {
    for (auto&& entry: _workerConnector) {
        entry.second->stop();
    }
}


void Messenger::cancel(string const& worker,
                       string const& id) {

    // Forward the request to the corresponding worker
    _connector(worker)->cancel(id);
}


bool Messenger::exists(string const& worker,
                       string const& id) const {

    // Forward the request to the corresponding worker
    return _connector(worker)->exists(id);
}


MessengerConnector::Ptr const& Messenger::_connector(string const& worker)  const {

    if (0 == _workerConnector.count(worker))
        throw invalid_argument(
                 "Messenger::" + string(__func__) + "   unknown worker: " + worker);
    return _workerConnector.at(worker);
}

}}} // namespace lsst::qserv::replica
