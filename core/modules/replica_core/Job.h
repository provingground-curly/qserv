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
#ifndef LSST_QSERV_REPLICA_CORE_JOB_H
#define LSST_QSERV_REPLICA_CORE_JOB_H

/// Job.h declares:
///
/// class Job
/// (see individual class documentation for more information)

// System headers

#include <memory>       // shared_ptr, enable_shared_from_this
#include <mutex>
#include <string>

// Qserv headers

#include "replica_core/Controller.h"
#include "replica_core/RequestTracker.h"

// Forward declarations

// This header declarations

namespace lsst {
namespace qserv {
namespace replica_core {

/**
  * Class Job is a base class for a family of replication jobs within
  * the master server.
  */
class Job
    :   public std::enable_shared_from_this<Job>  {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<Job> pointer;

    /// Primary public state of the job
    enum State {

        /// The job has been constructed, and no attempt to execute it has
        /// been made.
        CREATED,
        
        /// The job is in a progress
        IN_PROGRESS,
        
        /// The job is finihed. See extended status for more details
        /// (the completion status, etc.)
        FINISHED
    };

    /// Return the string representation of the primary state
    static std::string state2string (State state) ;

    /// Refined public sub-state of the job once it's FINISHED as per
    /// the above defined primary state.
    enum ExtendedState {

        /// No extended state exists at this time        
        NONE,

        /// The job has been fully implemented
        SUCCEEDED,

        /// The job has failed
        FAILED,

        /// Expired due to a timeout (as per the Configuration)
        EXPIRED,
        
        /// Explicitly cancelled on the client-side (similar to EXPIRED)
        CANCELLED
    };

    /// Return the string representation of the extended state
    static std::string state2string (ExtendedState state) ;

    /// Return the string representation of the combined state
    static std::string state2string (State state, ExtendedState extendedState) {
        return state2string(state) + "::" +state2string(extendedState);
    }
    // Default construction and copy semantics are prohibited

    Job () = delete;
    Job (Job const&) = delete;
    Job& operator= (Job const&) = delete;

    /// Destructor
    virtual ~Job ();

    /// Return a reference to the Controller,
    Controller::pointer controller () { return _controller; }

    /// Return a string representing a type of a job.
    std::string const& type () const { return _type; }

    /// Return a unique identifier of the job
    std::string const& id () const { return _id; }

    /// Return the primary status of the job
    State state () const { return _state; }

    /// Return the extended state of the job when it's finished
    ExtendedState extendedState () const { return _extendedState; }

    /**
     * Reset the state (if needed) and begin processing the job.
     */
    void start ();

    /**
     * Explicitly cancel the job and all relevant requests which may be still
     * in flight.
     */
    void cancel ();

    /// Return the context string for debugging and diagnostic printouts
    std::string context () const;

protected:

    /// Return shared pointer of the desired subclass (no dynamic type checking)
    template <class T>
    std::shared_ptr<T> shared_from_base () {
        return std::static_pointer_cast<T>(shared_from_this());
    }

    /**
     * Construct the request with the pointer to the services provider.
     *
     * @param controller - for launching requests
     * @param type       - its type name
     */
    Job (Controller::pointer const& controller,
         std::string const&         type);

    /**
      * This method is supposed to be provided by subclasses for additional
      * subclass-specific actions to begin processing the request.
      */
    virtual void startImpl ()=0;

    /**
      * This method is supposed to be provided by subclasses
      * to finalize request processing as required by the subclass.
      */
    virtual void cancelImpl ()=0;

    /**
     * Ensure the object is in the deseride internal state. Throw an
     * exception otherwise.
     *
     * NOTES: normally this condition should never been seen unless
     *        there is a problem with the application implementation
     *        or the underlying run-time system.
     * 
     * @throws std::logic_error
     */
    void assertState (State desiredState) const;

    /**
     * Set the desired primary and extended state.
     *
     * The change of the state is done via a method to allow extra actions
     * at this step, such as:
     *
     * - reporting change state in a debug stream
     * - verifying the correctness of the state transition
     */
    void setState (State         state,
                   ExtendedState extendedStat);
    
protected:

    /// The unique identifier of the job
    std::string _id;

    /// The Controller for performing requests
    Controller::pointer _controller;

    /// The type of the job
    std::string _type;

    /// Primary state of the job
    State _state;

    /// Extended state of the job
    ExtendedState _extendedState;

    // Start and end times (milliseconds since UNIX Epoch)

    uint64_t _beginTime;
    uint64_t _endTime;

    /// Mutex guarding internal state
    mutable std::mutex _mtx;
    
    /// For tracking on-going requests
    AnyRequestTracker _tracker;
};

}}} // namespace lsst::qserv::replica_core

#endif // LSST_QSERV_REPLICA_CORE_JOB_H