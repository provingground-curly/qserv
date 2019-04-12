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
#include "replica/WorkerRequest.h"

// System headers
#include <stdexcept>

// Qserv headers
#include "lsst/log/Log.h"
#include "replica/ServiceProvider.h"
#include "replica/SuccessRateGenerator.h"
#include "util/BlockPost.h"

using namespace std;

namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica.WorkerRequest");

/// Maximum duration for the request execution
unsigned int const maxDurationMillisec = 10000;

/// Random interval for the incremental execution
lsst::qserv::util::BlockPost incrementIvalMillisec(1000, 2000);

/// Random generator of success/failure rates
lsst::qserv::replica::SuccessRateGenerator successRateGenerator(0.9);

} /// namespace

namespace lsst {
namespace qserv {
namespace replica {

util::Mutex WorkerRequest::_mtxDataFolderOperations;
util::Mutex WorkerRequest::_mtx;


string WorkerRequest::status2string(CompletionStatus status) {
    switch (status) {
        case STATUS_NONE:          return "STATUS_NONE";
        case STATUS_IN_PROGRESS:   return "STATUS_IN_PROGRESS";
        case STATUS_IS_CANCELLING: return "STATUS_IS_CANCELLING";
        case STATUS_CANCELLED:     return "STATUS_CANCELLED";
        case STATUS_SUCCEEDED:     return "STATUS_SUCCEEDED";
        case STATUS_FAILED:        return "STATUS_FAILED";
    }
    throw logic_error(
            "WorkerRequest::" + string(__func__) + "  unhandled status: " + to_string(status));
}


string WorkerRequest::status2string(CompletionStatus status,
                                    ExtendedCompletionStatus extendedStatus) {
    return status2string(status) + "::" + replica::status2string(extendedStatus);
}


WorkerRequest::WorkerRequest(ServiceProvider::Ptr const& serviceProvider,
                             string const& worker,
                             string const& type,
                             string const& id,
                             int priority)
    :   _serviceProvider(serviceProvider),
        _worker(worker),
        _type(type),
        _id(id),
        _priority(priority),
        _status(STATUS_NONE),
        _extendedStatus(ExtendedCompletionStatus::EXT_STATUS_NONE),
        _performance(),
        _durationMillisec(0) {

    serviceProvider->assertWorkerIsValid(worker);
}


WorkerRequest::ErrorContext WorkerRequest::reportErrorIf(
                                                bool errorCondition,
                                                ExtendedCompletionStatus extendedStatus,
                                                string const& errorMsg) {
    WorkerRequest::ErrorContext errorContext;
    if (errorCondition) {
        errorContext.failed = true;
        errorContext.extendedStatus = extendedStatus;
        LOGS(_log, LOG_LVL_ERROR, context() << "execute" << errorMsg);
    }
    return errorContext;
}


void WorkerRequest::start() {

    LOGS(_log, LOG_LVL_DEBUG, context(__func__));    

    util::Lock lock(_mtx, context(__func__));

    switch (status()) {

        case STATUS_NONE:
            setStatus(lock, STATUS_IN_PROGRESS);
            break;

        default:
            throw logic_error(
                    context(__func__) + "  not allowed while in status: " +
                    WorkerRequest::status2string(status()));
    }
}


bool WorkerRequest::execute() {

    LOGS(_log, LOG_LVL_DEBUG, context(__func__));

    util::Lock lock(_mtx, context(__func__));

    // Simulate request 'processing' for some maximum duration of time (milliseconds)
    // while making a progress through increments of random duration of time.
    // Success/failure modes will be also simulated using the corresponding generator.

   switch (status()) {

        case STATUS_IN_PROGRESS:
            break;

        case STATUS_IS_CANCELLING:
            setStatus(lock, STATUS_CANCELLED);
            throw WorkerRequestCancelled();

        default:
            throw logic_error(
                    context(__func__) + "  not allowed while in status: " +
                    WorkerRequest::status2string(status()));
    }

    _durationMillisec += ::incrementIvalMillisec.wait();

    if (_durationMillisec < ::maxDurationMillisec) return false;

    setStatus(lock, ::successRateGenerator.success() ?
                        STATUS_SUCCEEDED :
                        STATUS_FAILED);
    return true;
}


void WorkerRequest::cancel() {

    LOGS(_log, LOG_LVL_DEBUG, context(__func__));

    util::Lock lock(_mtx, context(__func__));

    switch (status()) {

        case STATUS_NONE:
        case STATUS_CANCELLED:
            setStatus(lock, STATUS_CANCELLED);
            break;

        case STATUS_IN_PROGRESS:
        case STATUS_IS_CANCELLING:
            setStatus(lock, STATUS_IS_CANCELLING);
            break;

        /* Nothing to be done to completed requests */
        case WorkerRequest::STATUS_SUCCEEDED:
        case WorkerRequest::STATUS_FAILED:
            break;
    }
}


void WorkerRequest::rollback() {

    LOGS(_log, LOG_LVL_DEBUG, context(__func__));

    util::Lock lock(_mtx, context(__func__));

    switch (status()) {

        case STATUS_NONE:
        case STATUS_IN_PROGRESS:
            setStatus(lock, STATUS_NONE);
            break;

        case STATUS_IS_CANCELLING:
            setStatus(lock, STATUS_CANCELLED);
            throw WorkerRequestCancelled();
            break;

        default:
            throw logic_error(
                    context(__func__) + "  not allowed while in status: " +
                    WorkerRequest::status2string(status()));
    }
}


void WorkerRequest::stop() {
    LOGS(_log, LOG_LVL_DEBUG, context(__func__));    
    util::Lock lock(_mtx, context(__func__));
    setStatus(lock, STATUS_NONE);
}


void WorkerRequest::setStatus(util::Lock const& lock,
                              CompletionStatus status,
                              ExtendedCompletionStatus extendedStatus) {

    LOGS(_log, LOG_LVL_DEBUG, context(__func__) << "  "
         << WorkerRequest::status2string(_status, _extendedStatus) << " -> "
         << WorkerRequest::status2string( status,  extendedStatus));

    switch (status) {
        case STATUS_NONE:
            _performance.start_time  = 0;
            _performance.finish_time = 0;
            break;

        case STATUS_IN_PROGRESS:
            _performance.setUpdateStart();
            _performance.finish_time = 0;
            break;

        case STATUS_IS_CANCELLING:
            break;

        case STATUS_CANCELLED:

            // Set the start time to some meaningful value in case if the request was
            // cancelled while sitting in the input queue

            if (0 == _performance.start_time) _performance.setUpdateStart();

            _performance.setUpdateFinish();
            break;

        case STATUS_SUCCEEDED:
        case STATUS_FAILED:
            _performance.setUpdateFinish();
            break;

        default:
            throw logic_error(
                    context(__func__) + "  unhandled status: " +
                    to_string(status));
    }

    // ATTENTION: the top-level status is the last to be modified in
    // the state transition to ensure clients will see a consistent state
    // of the object.

    _extendedStatus = extendedStatus;
    _status = status;
}

}}} // namespace lsst::qserv::replica
