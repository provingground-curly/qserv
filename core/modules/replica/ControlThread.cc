/*
 * LSST Data Management System
 * Copyright 2018 LSST Corporation.
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
#include "replica/ControlThread.h"

// System headers
#include <stdexcept>
#include <thread>

// Qserv headers
#include "replica/QservSyncJob.h"
#include "util/BlockPost.h"

namespace lsst {
namespace qserv {
namespace replica {

ControlThreadError::ControlThreadError(util::Issue::Context const& ctx,
                                       std::string const& message)
    :   util::Issue(ctx, "ControlThread: " + message) {
}


bool ControlThread::start() {

    debug("starting...");

    util::Lock lock(_mtx, context() + "start");

    if (_isRunning.exchange(true)) {
        return true;
    }
    std::thread t(
        std::bind(
            &ControlThread::_startImpl,
            shared_from_this()
        )
    );
    t.detach();
    
    return false;
}


bool ControlThread::stop() {
 
    debug("stopping...");

    util::Lock lock(_mtx, context() + "stop");

    if (not _isRunning) return true;

    _stopRequested = true;

    return false;
}


bool ControlThread::startAndWait(WaitEvaluatorType const& abortWait) {

    bool const wasRunning = start();

    auto self = shared_from_this();

    util::BlockPost blockPost(1000, 1001);  // ~1s
    while (isRunning()) {
        if ((nullptr != abortWait) and abortWait(self)) break;
        blockPost.wait();
    }
    
    return wasRunning;
}


ControlThread::ControlThread(Controller::Ptr const& controller,
                             std::string const& name,
                             CallbackType const& onTerminated)
    :   _controller(controller),
        _name(name),
        _onTerminated(onTerminated),
        _isRunning(false),
        _stopRequested(false),
        _log(LOG_GET("lsst.qserv.replica.ControlThread")) {

    debug("created");
}


std::string ControlThread::context() const {
    return _name + " ";
}


void ControlThread::sync(unsigned int qservSyncTimeoutSec,
                         bool forceQservSync) {
    launch<QservSyncJob>("QservSyncJob",
                         qservSyncTimeoutSec,
                         forceQservSync);
}


void ControlThread::_startImpl() {

    // By design of this class, any but ControlThreadStopped exceptions thrown
    // by a subclass-specific method called below will be interpreted as
    // the "abnormal" termination condition to be reported via an optinal
    // callback.

    bool terminated = false;
    try {
        debug("started");
        run();
        debug("stopped");

    } catch (ControlThreadStopped const&) {
        debug("stopped");

    } catch (std::exception const& ex) {
        error("terminated, exception: " + std::string(ex.what()));
        terminated = true;
    }

    // This lock is needed to ensure thread safety when making changes
    // to the object's state.
    //
    // Note that the object state needs to be updated before making
    // the emergency upstream notification. The notification is made
    // via a non-blocking mechanism by scheduling the callback to be
    // run in a different thread with the life expectancy of the current
    // object guaranteed through a copy of a shared pointer bound as
    // a parameter of the callback.

    util::Lock lock(_mtx, context() + "_startImpl");
    
    _stopRequested = false;
    _isRunning     = false;

    if (terminated) {
        serviceProvider()->io_service().post(
            std::bind(
                _onTerminated,
                shared_from_this()
            )
        );
    }
}


}}} // namespace lsst::qserv::replica