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
#include "replica_core/Job.h"

// System headers

#include <stdexcept>

// Qserv headers

#include "lsst/log/Log.h"
#include "replica_core/Common.h"            // Generators::uniqueId()
#include "replica_core/Performane.h"        // PerformanceUtils::now()

// This macro to appear witin each block which requires thread safety

#define LOCK_GUARD \
std::lock_guard<std::mutex> lock(_mtx)

namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica_core.Job");

} /// namespace

namespace lsst {
namespace qserv {
namespace replica_core {

std::mutex
Job::_mtx;

std::string
Job::state2string (State state) {
    switch (state) {
        case CREATED:     return "CREATED";
        case IN_PROGRESS: return "IN_PROGRESS";
        case FINISHED:    return "FINISHED";
    }
    throw std::logic_error (
                "incomplete implementation of method Job::state2string(State)");
}

std::string
Request::state2string (ExtendedState state) {
    switch (state) {
        case NONE:                 return "NONE";
        case SUCCESS:              return "SUCCESS";
        case FAILED:    return "FAILED";
        case EXPIRED:   return "EXPIRED";
        case CANCELLED: return "CANCELLED";
    }
    throw std::logic_error (
                "incomplete implementation of method Request::state2string(ExtendedState)");
}

Job::Job (Controller::pointer const& controller,
          std::string const&         type) {

    :   _id         (Generators::uniqueId()),
        _controller (controller),
        _type       (type),

        _state         (State::CREATED),
        _extendedState (ExtendedState::NONE),

        _startTime (0),
        _endTime   (0) {
}

Job::~Job () {
}

std::string
Job::context () const {
    return "JOB [id=" + _id + ", type=" + _type + ", state=" + state2string(_state, _extendedState) + "]  ";
}

void
Job::start () {

    LOCK_GUARD;

    LOGS(_log, LOG_LVL_DEBUG, context() << "start");

    startImpl();
}

void
Job::cancel () {

    LOCK_GUARD;

    LOGS(_log, LOG_LVL_DEBUG, context() << "cancel");

    cancelImpl();
}

void
Job::assertState (State state) const {
    if (state != _state)
        throw std::logic_error (
            "wrong state " + state2string(state) + " instead of " + state2string(_state));
}

void
Job::setState (State         state,
               ExtendedState extendedStat) {
    LOGS(_log, LOG_LVL_DEBUG, context() << "setState  state=" << state2string(state, extendedState));
    _state        = state;
    _extendedStat = extendedStat;
}
    
}}} // namespace lsst::qserv::replica_core