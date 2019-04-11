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
#include "replica_core/DeleteWorkerJob.h"

// System headers

#include <algorithm>
#include <stdexcept>
#include <tuple>

// Qserv headers

#include "lsst/log/Log.h"
#include "replica_core/BlockPost.h"
#include "replica_core/ErrorReporting.h"
#include "replica_core/ServiceManagementRequest.h"
#include "replica_core/ServiceProvider.h"


// This macro to appear witin each block which requires thread safety

#define LOCK_GUARD \
std::lock_guard<std::mutex> lock(_mtx)

namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica_core.DeleteWorkerJob");

using namespace lsst::qserv::replica_core;

/**
 * Count the total number of entries in the input collection,
 * the number of finished entries, nd the total number of succeeded
 * entries.
 *
 * The "entries" in this context are either derivatives of the Request
 * or Job types.
 *
 * @param collection - a collection of entries to be analyzed
 * @return - a tuple of three elements
 */
template <class T>
std::tuple<size_t,size_t,size_t> counters (std::list<typename T::pointer> const& collection) {
    size_t total    = 0;
    size_t finished = 0;
    size_t success  = 0;
    for (auto const& ptr: collection) {
        total++;
        if (ptr->state() == T::State::FINISHED) {
            finished++;
            if (ptr->extendedState() == T::ExtendedState::SUCCESS) {
                success++;
            }
        }
    }
    return std::make_tuple(total, finished, success);
}


/**
 * Track requests or jobs
 *
 * @param collection - a collection of entries to be analyzed
 */
template <class T>
void
track (std::list<typename T::pointer> const& collection,
       std::string const&                    scope,
       bool                                  progressReport,
       std::ostream&                         os) {

    BlockPost blockPost (1000, 2000);

    while (true) {
        blockPost.wait();

        std::tuple<size_t,size_t,size_t> t = ::counters<T> (collection);

        size_t const launched = std::get<0>(t);
        size_t const finished = std::get<1>(t);
        size_t const success  = std::get<1>(t);
            
        if (progressReport)
            os  << "DeleteWorkerJob::track()  " << scope << "  "
                << "launched: " << launched << ", "
                << "finished: " << finished << ", "
                << "success: "  << success
                << std::endl;

        if (finished == launched) break;
    }
}


} /// namespace

namespace lsst {
namespace qserv {
namespace replica_core {

DeleteWorkerJob::pointer
DeleteWorkerJob::create (std::string const&         worker,
                         bool                       permanentDelete,
                         Controller::pointer const& controller,
                         callback_type              onFinish,
                         bool                       bestEffort,
                         int                        priority,
                         bool                       exclusive,
                         bool                       preemptable) {
    return DeleteWorkerJob::pointer (
        new DeleteWorkerJob (worker,
                             permanentDelete,
                             controller,
                             onFinish,
                             bestEffort,
                             priority,
                             exclusive,
                             preemptable));
}

DeleteWorkerJob::DeleteWorkerJob (std::string const&         worker,
                                  bool                       permanentDelete,
                                  Controller::pointer const& controller,
                                  callback_type              onFinish,
                                  bool                       bestEffort,
                                  int                        priority,
                                  bool                       exclusive,
                                  bool                       preemptable)

    :   Job (controller,
             "DELETE_WORKER",
             priority,
             exclusive,
             preemptable),

        _worker          (worker),
        _permanentDelete (permanentDelete),

        _onFinish   (onFinish),
        _bestEffort (bestEffort),
        
        _numLaunched (0),
        _numFinished (0),
        _numSuccess  (0) {
}

DeleteWorkerJob::~DeleteWorkerJob () {
}

DeleteWorkerJobResult const&
DeleteWorkerJob::getReplicaData () const {

    LOGS(_log, LOG_LVL_DEBUG, context() << "getReplicaData");

    if (_state == State::FINISHED)  return _replicaData;

    throw std::logic_error (
        "DeleteWorkerJob::getReplicaData  the method can't be called while the job hasn't finished");
}

void
DeleteWorkerJob::track (bool          progressReport,
                        bool          errorReport,
                        bool          chunkLocksReport,
                        std::ostream& os) const {

    if (_state == State::FINISHED) return;
    
    ::track<FindAllRequest> (_findAllRequests, "_findAllRequests", progressReport, os);
    if (errorReport) {

        std::tuple<size_t,size_t,size_t> t = ::counters<FindAllRequest> (_findAllRequests);
        size_t const launched = std::get<0>(t);
        size_t const success  = std::get<1>(t);

        if (launched - success)
            replica_core::reportRequestState (_findAllRequests, os);
    }
    ::track<FindAllJob>   (_findAllJobs,   "_findAllJobs",   progressReport, os);
    ::track<ReplicateJob> (_replicateJobs, "_replicateJobs", progressReport, os);

    // The last step is needed to le the job to finalize its state after
    // finishing all activities.
    BlockPost blockPost (1000, 2000);
    while (state() != State::FINISHED) {
        LOGS(_log, LOG_LVL_DEBUG, context() << "track");
        blockPost.wait();
    }
}

void
DeleteWorkerJob::startImpl () {

    LOGS(_log, LOG_LVL_DEBUG, context() << "startImpl");

    BlockPost blockPost (1000, 2000);

    auto self = shared_from_base<DeleteWorkerJob>();

    // Check the status of the worker service, and if it's still running
    // try to get as much info from it as possible
    ServiceStatusRequest::pointer const statusRequest =
        _controller->statusOfWorkerService (
            _worker,
            nullptr,    /* onFinish  -- not needed here because tracking the request -- */
            _id,        /* jobId */
            10          /* requestExpirationIvalSec */);

    while (statusRequest->state() != Request::State::FINISHED) {
        LOGS(_log, LOG_LVL_DEBUG, context() << "wait for worker service status");
        blockPost.wait();
    }
    if (statusRequest->extendedState() == Request::ExtendedState::SUCCESS) {
        if (statusRequest->getServiceState().state == ServiceState::State::RUNNING) {
            
            // Make sure the service won't be executing any other "leftover"
            // requests which may be interfeering with the current job's requests
            ServiceDrainRequest::pointer const drainRequest =
                _controller->drainWorkerService (
                    _worker,
                    nullptr,    /* onFinish  -- not needed here because tracking the request -- */
                    _id,        /* jobId */
                    10          /* requestExpirationIvalSec */);

            while (drainRequest->state() != Request::State::FINISHED) {
                LOGS(_log, LOG_LVL_DEBUG, context() << "wait for worker service drain");
                blockPost.wait();
            }
            if (drainRequest->extendedState() == Request::ExtendedState::SUCCESS) {
                if (drainRequest->getServiceState().state == ServiceState::State::RUNNING) {
                    
                    // Try to get the most recent state the worker's replicas
                    // for all known databases
                    for (auto const& database: _controller->serviceProvider().config()->databases()) {
                        FindAllRequest::pointer const request =
                            _controller->findAllReplicas (
                                _worker,
                                database,
                                [self] (FindAllRequest::pointer ptr) {
                                    self->onRequestFinish (ptr);
                                }
                            );
                        _findAllRequests.push_back(request);
                        _numLaunched++;
                    }

                    // The rest will be happening in a method processing the completion
                    // of the above launched requests.

                    setState(State::IN_PROGRESS);
                    return;
                }
            }
        }
    }
    
    // Since the worker is not available then go straight to a point
    // at which we'll be changing its state within the replication system
    disableWorker();

    setState(State::IN_PROGRESS);
    return;
}

void
DeleteWorkerJob::cancelImpl () {

    LOGS(_log, LOG_LVL_DEBUG, context() << "cancelImpl");

    // To ensure no lingering "side effects" will be left after cancelling this
    // job the request cancellation should be also followed (where it makes a sense)
    // by stopping the request at corresponding worker service.

    for (auto const& ptr: _findAllRequests) {
        ptr->cancel();
        if (ptr->state() != Request::State::FINISHED)
            _controller->stopReplicaFindAll (
                ptr->worker(),
                ptr->id(),
                nullptr,    /* onFinish */
                true,       /* keepTracking */
                _id         /* jobId */);
    }

    // Stop chained jobs (if any) as well

    for (auto const& ptr: _findAllJobs)   ptr->cancel();
    for (auto const& ptr: _replicateJobs) ptr->cancel();

    setState(State::FINISHED, ExtendedState::CANCELLED);
}

void
DeleteWorkerJob::notify () {

    LOGS(_log, LOG_LVL_DEBUG, context() << "notify");

    if (_onFinish) {
        auto self = shared_from_base<DeleteWorkerJob>();
        _onFinish(self);
    }
}

void
DeleteWorkerJob::onRequestFinish (FindAllRequest::pointer request) {

    LOGS(_log, LOG_LVL_DEBUG, context()
         << "onRequestFinish"
         << "  worker="   << request->worker()
         << "  database=" << request->database());

    do {
        // This lock will be automatically release beyon this scope
        // to allow client notifications (see the end of the method)
        LOCK_GUARD;

        _numFinished++;

        // Ignore the callback if the job was cancelled   
        if (_state == State::FINISHED) {
            return;
        }    
        if (request->extendedState() == Request::ExtendedState::SUCCESS) {
            _numSuccess++;
        }

    } while (false);

    // Evaluate the status of on-going operations to see if the job
    // has finished. If so then proceed to the next stage of the job.
    //
    // ATTENTION: we don't care about the completion status of the requests
    // because the're related to a worker which is going t be removed, and
    // this worker may already be experiencing problems.
    //
    if (_numFinished == _numLaunched) {
        disableWorker();
    }
}

void
DeleteWorkerJob::disableWorker () {

    LOGS(_log, LOG_LVL_DEBUG, context() << "disableWorker");

    // Temporary disable this worker from the configuration. If it's requsted
    // to be permanently deleted this will be done only after all other relevamnt
    // operations of this job will be done.
    _controller->serviceProvider().config()->disableWorker(_worker);

    // Launch the chained jobs to get chunk disposition within the rest
    // of the cluster

    _numLaunched = 0;
    _numFinished = 0;
    _numSuccess  = 0;

    auto self = shared_from_base<DeleteWorkerJob>();

    for (auto const& databaseFamily: _controller->serviceProvider().config()->databaseFamilies()) {
        FindAllJob::pointer job = FindAllJob::create (
            databaseFamily,
            _controller,
            [self] (FindAllJob::pointer job) {
                self->onJobFinish(job);
            }
        );
        job->start();
        _findAllJobs.push_back(job);
        _numLaunched++;
    }
}

void
DeleteWorkerJob::onJobFinish (FindAllJob::pointer job) {

    LOGS(_log, LOG_LVL_DEBUG, context() << "onJobFinish(FindAllJob) "
         << " databaseFamily: " << job->databaseFamily());

    do {
        // This lock will be automatically release beyon this scope
        // to allow client notifications (see the end of the method)
        LOCK_GUARD;

        _numFinished++;

        // Ignore the callback if the job was cancelled (or otherwise failed)
        if (_state == State::FINISHED) return;
    
        // Do not proceed with the rest unless running the jobs
        // under relaxed condition.
    
        if (!_bestEffort && (job->extendedState() != ExtendedState::SUCCESS)) {
            setState(State::FINISHED, ExtendedState::FAILED);
            break;
        }
        if (job->extendedState() == ExtendedState::SUCCESS) {
            _numSuccess++;
        }        
        if (_numFinished == _numLaunched) {
    
            // Launch chained jobs to ensure the minimal replication level
            // which might be affected by the worker removal.
    
            _numLaunched = 0;
            _numFinished = 0;
            _numSuccess  = 0;
    
            auto self = shared_from_base<DeleteWorkerJob>();
    
            for (auto const& databaseFamily: _controller->serviceProvider().config()->databaseFamilies()) {
                ReplicateJob::pointer job = ReplicateJob::create (
                    databaseFamily,
                    0,  /* numReplicas -- pull from Configuration */
                    _controller,
                    [self] (ReplicateJob::pointer job) {
                        self->onJobFinish(job);
                    }
                );
                job->start();
                _replicateJobs.push_back(job);
                _numLaunched++;
            }
        }

    } while (false);
    
    // Note that access to the job's public API should not be locked while
    // notifying a caller (if the callback function was povided) in order to avoid
    // the circular deadlocks.

    if (_state == State::FINISHED)
        notify ();
}

void
DeleteWorkerJob::onJobFinish (ReplicateJob::pointer job) {

    LOGS(_log, LOG_LVL_DEBUG, context() << "onJobFinish(ReplicateJob) "
         << " databaseFamily: " << job->databaseFamily()
         << " numReplicas: " << job->numReplicas()
         << " state: " << Job::state2string(job->state(), job->extendedState()));

    do {
        // This lock will be automatically release beyond this scope
        // to allow client notifications (see the end of the method)
        LOCK_GUARD;

        _numFinished++;

        // Ignore the callback if the job was cancelled (or otherwise failed)
        if (_state == State::FINISHED) return;

        // Do not proceed with the rest unless running the jobs
        // under relaxed condition.
    
        if (!_bestEffort && (job->extendedState() != ExtendedState::SUCCESS)) {
            setState(State::FINISHED, ExtendedState::FAILED);
            break;
        }
    
        if (job->extendedState() == ExtendedState::SUCCESS) {
            _numSuccess++;

            LOGS(_log, LOG_LVL_DEBUG, context() << "onJobFinish(ReplicateJob)  "
                 << "job->getReplicaData().chunks.size(): " << job->getReplicaData().chunks.size());

            // Merge results into the current job's result object
            _replicaData.chunks[job->databaseFamily()] = job->getReplicaData().chunks;
        }    
        if (_numFinished == _numLaunched) {

            // Construct a collection of orphan replicas if possible

            ReplicaInfoCollection replicas;
            if (_controller->serviceProvider().databaseServices()->findWorkerReplicas(replicas, _worker)) {
                for (ReplicaInfo const& replica: replicas) {
                    unsigned int const chunk    = replica.chunk();
                    std::string const& database = replica.database();

                    bool replicated = false;
                    for (auto const& databaseFamilyEntry: _replicaData.chunks) {
                        auto const& chunks = databaseFamilyEntry.second;
                        replicated = replicated or
                            (chunks.count(chunk) and chunks.at(chunk).count(database));
                    }
                    if (not replicated)
                        _replicaData.orphanChunks[chunk][database] = replica;
                }
            }
            
            // TODO: if the list of orphan chunks is not empty then consider bringing
            // back the disabled worker (if the service still responds) in the read-only
            // mode and try using it for redistributing those chunks accross the cluster.
            //
            // NOTE: this could be a complicated procedure which needs to be thought
            // through.
            ;

            // Do this only if requested, and only in case of the successful
            // completion of the job
            if (_permanentDelete)
                _controller->serviceProvider().config()->deleteWorker (_worker);

            setState(State::FINISHED, ExtendedState::SUCCESS);
            break;
        }

    } while (false);
    
    // Note that access to the job's public API should not be locked while
    // notifying a caller (if the callback function was povided) in order to avoid
    // the circular deadlocks.

    if (_state == State::FINISHED)
        notify ();
}

}}} // namespace lsst::qserv::replica_core