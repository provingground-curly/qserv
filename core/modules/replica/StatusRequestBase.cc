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
#include "replica/StatusRequestBase.h"

// System headers
#include <stdexcept>

// Third party headers
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

// Qserv headers
#include "lsst/log/Log.h"
#include "replica/ProtocolBuffer.h"
#include "replica/ServiceProvider.h"

using namespace std;

namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica.StatusRequest");

} /// namespace

namespace lsst {
namespace qserv {
namespace replica {

StatusRequestBase::StatusRequestBase(ServiceProvider::Ptr const& serviceProvider,
                                     boost::asio::io_service& io_service,
                                     char const* requestTypeName,
                                     string const& worker,
                                     string const& targetRequestId,
                                     proto::ReplicationReplicaRequestType replicaRequestType,
                                     bool keepTracking,
                                     shared_ptr<Messenger> const& messenger)
    :   RequestMessenger(serviceProvider,
                         io_service,
                         requestTypeName,
                         worker,
                         0,    /* priority */
                         keepTracking,
                         false /* allowDuplicate */,
                         messenger),
        _targetRequestId(targetRequestId),
        _replicaRequestType(replicaRequestType) {
}


void StatusRequestBase::startImpl(util::Lock const& lock) {
    LOGS(_log, LOG_LVL_DEBUG, context() << "startImpl");
    sendImpl(lock);
}


void StatusRequestBase::wait(util::Lock const& lock) {

    LOGS(_log, LOG_LVL_DEBUG, context() << "wait");

    // Always need to set the interval before launching the timer.

    timer().expires_from_now(boost::posix_time::seconds(timerIvalSec()));
    timer().async_wait(
        boost::bind(
            &StatusRequestBase::awaken,
            shared_from_base<StatusRequestBase>(),
            boost::asio::placeholders::error
        )
    );
}


void StatusRequestBase::awaken(boost::system::error_code const& ec) {

    LOGS(_log, LOG_LVL_DEBUG, context() << "awaken");

    if (isAborted(ec)) return;

    // IMPORTANT: the final state is required to be tested twice. The first time
    // it's done in order to avoid deadlock on the "in-flight" callbacks reporting
    // their completion while the request termination is in a progress. And the second
    // test is made after acquiring the lock to recheck the state in case if it
    // has transitioned while acquiring the lock.

    if (state() == State::FINISHED) return;

    util::Lock lock(_mtx, context() + "awaken");

    if (state() == State::FINISHED) return;

    sendImpl(lock);
}


void StatusRequestBase::sendImpl(util::Lock const& lock) {

    LOGS(_log, LOG_LVL_DEBUG, context() << "sendImpl");

    // Serialize the Status message header and the request itself into
    // the network buffer.

    buffer()->resize();

    proto::ReplicationRequestHeader hdr;
    hdr.set_id(id());
    hdr.set_type(proto::ReplicationRequestHeader::REQUEST);
    hdr.set_management_type(proto::ReplicationManagementRequestType::REQUEST_STATUS);

    buffer()->serialize(hdr);

    proto::ReplicationRequestStatus message;
    message.set_id(_targetRequestId);
    message.set_replica_type(_replicaRequestType);

    buffer()->serialize(message);

    send(lock);
}


void StatusRequestBase::analyze(bool success,
                                proto::ReplicationStatus status) {

    LOGS(_log, LOG_LVL_DEBUG, context() << "analyze  success=" << (success ? "true" : "false"));

    // This method is called on behalf of an asynchronous callback fired
    // upon a completion of the request within method send() - the only
    // client of analyze(). So, we should take care of proper locking and watch
    // for possible state transition which might occur while the async I/O was
    // still in a progress.

    // IMPORTANT: the final state is required to be tested twice. The first time
    // it's done in order to avoid deadlock on the "in-flight" callbacks reporting
    // their completion while the request termination is in a progress. And the second
    // test is made after acquiring the lock to recheck the state in case if it
    // has transitioned while acquiring the lock.

    if (state() == State::FINISHED) return;

    util::Lock lock(_mtx, context() + "analyze");

    if (state() == State::FINISHED) return;

    if (not success) {
        finish(lock, CLIENT_ERROR);
        return;
    }

    switch (status) {

        case proto::ReplicationStatus::SUCCESS:

            saveReplicaInfo();

            finish(lock, SUCCESS);
            break;

        case proto::ReplicationStatus::QUEUED:
            if (keepTracking()) wait(lock);
            else                finish(lock, SERVER_QUEUED);
            break;

        case proto::ReplicationStatus::IN_PROGRESS:
            if (keepTracking()) wait(lock);
            else                finish(lock, SERVER_IN_PROGRESS);
            break;

        case proto::ReplicationStatus::IS_CANCELLING:
            if (keepTracking()) wait(lock);
            else                finish(lock, SERVER_IS_CANCELLING);
            break;

        case proto::ReplicationStatus::BAD:
            finish(lock, SERVER_BAD);
            break;

        case proto::ReplicationStatus::FAILED:
            finish(lock, SERVER_ERROR);
            break;

        case proto::ReplicationStatus::CANCELLED:
            finish(lock, SERVER_CANCELLED);
            break;

        default:
            throw logic_error(
                        "StatusRequestBase::analyze() unknown status '"
                        + proto::ReplicationStatus_Name(status) +
                        "' received from server");
    }
}

}}} // namespace lsst::qserv::replica
