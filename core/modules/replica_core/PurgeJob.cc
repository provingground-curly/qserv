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
#include "replica_core/PurgeJob.h"

// System headers

#include <algorithm>
#include <stdexcept>

// Qserv headers

#include "lsst/log/Log.h"
#include "replica_core/BlockPost.h"
#include "replica_core/ErrorReporting.h"
#include "replica_core/ServiceProvider.h"


// This macro to appear witin each block which requires thread safety

#define LOCK_GUARD \
std::lock_guard<std::mutex> lock(_mtx)

namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica_core.PurgeJob");

} /// namespace

namespace lsst {
namespace qserv {
namespace replica_core {

PurgeJob::pointer
PurgeJob::create (std::string const&         databaseFamily,
                  unsigned int               numReplicas,
                  Controller::pointer const& controller,
                  callback_type              onFinish,
                  bool                       bestEffort,
                  int                        priority,
                  bool                       exclusive,
                  bool                       preemptable) {
    return PurgeJob::pointer (
        new PurgeJob (databaseFamily,
                      numReplicas,
                      controller,
                      onFinish,
                      bestEffort,
                      priority,
                      exclusive,
                      preemptable));
}

PurgeJob::PurgeJob (std::string const&         databaseFamily,
                    unsigned int               numReplicas,
                    Controller::pointer const& controller,
                    callback_type              onFinish,
                    bool                       bestEffort,
                    int                        priority,
                    bool                       exclusive,
                    bool                       preemptable)

    :   Job (controller,
             "PURGE",
             priority,
             exclusive,
             preemptable),

        _databaseFamily (databaseFamily),

        _numReplicas (numReplicas ?
                      numReplicas :
                      controller->serviceProvider().config()->replicationLevel(databaseFamily)),

        _onFinish   (onFinish),
        _bestEffort (bestEffort),

        _numIterations  (0),
        _numFailedLocks (0),

        _numLaunched (0),
        _numFinished (0),
        _numSuccess  (0) {

    if (!_numReplicas)
        throw std::invalid_argument ("PurgeJob::PurgeJob ()  0 is not allowed for the number of replias");
}

PurgeJob::~PurgeJob () {
    // Make sure all chuks locked by this job are released
    _controller->serviceProvider().chunkLocker().release(_id);
}

PurgeJobResult const&
PurgeJob::getReplicaData () const {

    LOGS(_log, LOG_LVL_DEBUG, context() << "getReplicaData");

    if (_state == State::FINISHED)  return _replicaData;

    throw std::logic_error (
        "PurgeJob::getReplicaData  the method can't be called while the job hasn't finished");
}

void
PurgeJob::track (bool          progressReport,
                 bool          errorReport,
                 bool          chunkLocksReport,
                 std::ostream& os) const {

    if (_state == State::FINISHED) return;

    if (_findAllJob)
        _findAllJob->track (progressReport,
                            errorReport,
                            chunkLocksReport,
                            os);
    
    BlockPost blockPost (1000, 2000);

    while (_numFinished < _numLaunched) {
        blockPost.wait();
        if (progressReport)
            os  << "PurgeJob::track()  "
                << "launched: " << _numLaunched << ", "
                << "finished: " << _numFinished << ", "
                << "success: "  << _numSuccess
                << std::endl;

        if (chunkLocksReport)
            os  << "PurgeJob::track()  <LOCKED CHUNKS>  jobId: " << _id << "\n"
                << _controller->serviceProvider().chunkLocker().locked (_id);
    }
    if (progressReport)
        os  << "PurgeJob::track()  "
            << "launched: " << _numLaunched << ", "
            << "finished: " << _numFinished << ", "
            << "success: "  << _numSuccess
            << std::endl;

    if (chunkLocksReport)
        os  << "PurgeJob::track()  <LOCKED CHUNKS>  jobId: " << _id << "\n"
            << _controller->serviceProvider().chunkLocker().locked (_id);

    if (errorReport && _numLaunched - _numSuccess)
        replica_core::reportRequestState (_requests, os);
}

void
PurgeJob::startImpl () {

    LOGS(_log, LOG_LVL_DEBUG, context() << "startImpl  _numIterations=" << _numIterations);

    ++_numIterations;

    // Launch the chained job to get chunk disposition

    auto self = shared_from_base<PurgeJob>();

    _findAllJob = FindAllJob::create (
        _databaseFamily,
        _controller,
        [self] (FindAllJob::pointer job) {
            self->onPrecursorJobFinish();
        }
    );
    _findAllJob->start();

    setState(State::IN_PROGRESS);
}

void
PurgeJob::cancelImpl () {

    LOGS(_log, LOG_LVL_DEBUG, context() << "cancelImpl");

    // The algorithm will also clear resources taken by various
    // locally created objects.

    if (_findAllJob && (_findAllJob->state() != State::FINISHED))
        _findAllJob->cancel();

    _findAllJob = nullptr;

    // To ensure no lingering "side effects" will be left after cancelling this
    // job the request cancellation should be also followed (where it makes a sense)
    // by stopping the request at corresponding worker service.

    for (auto const& ptr: _requests) {
        ptr->cancel();
        if (ptr->state() != Request::State::FINISHED)
            _controller->stopReplicaDelete (
                ptr->worker(),
                ptr->id(),
                nullptr,    /* onFinish */
                true,       /* keepTracking */
                _id         /* jobId */);
    }
    _chunk2requests.clear();
    _requests.clear();

    _numFailedLocks = 0;

    _numLaunched = 0;
    _numFinished = 0;
    _numSuccess  = 0;

    setState(State::FINISHED, ExtendedState::CANCELLED);
}

void
PurgeJob::restart () {

    LOGS(_log, LOG_LVL_DEBUG, context() << "restart");
    
    if (_findAllJob or (_numLaunched != _numFinished))
        throw std::logic_error ("PurgeJob::restart ()  not allowed in this object state");

    _requests.clear();

    _numFailedLocks = 0;

    _numLaunched = 0;
    _numFinished = 0;
}

void
PurgeJob::notify () {

    LOGS(_log, LOG_LVL_DEBUG, context() << "notify");

    if (_onFinish) {
        auto self = shared_from_base<PurgeJob>();
        _onFinish(self);
    }
}

void
PurgeJob::onPrecursorJobFinish () {

    LOGS(_log, LOG_LVL_DEBUG, context() << "onPrecursorJobFinish");

    do {
        // This lock will be automatically release beyon this scope
        // to allow client notifications (see the end of the method)
        LOCK_GUARD;
    
        // Ignore the callback if the job was cancelled   
        if (_state == State::FINISHED) return;
    
        ////////////////////////////////////////////////////////////////////
        // Do not proceed with the replication effort unless running the job
        // under relaxed condition.
    
        if (!_bestEffort && (_findAllJob->extendedState() != ExtendedState::SUCCESS)) {
            setState(State::FINISHED, ExtendedState::FAILED);
            break;
        }

        /////////////////////////////////////////////////////////////////
        // Analyse results and prepare a deletion plan to remove extra
        // replocas for over-represented chunks
        //
        // IMPORTANT:
        //
        // - chunks which were found locked by some other job will not be deleted
        //
        // - when deciding on a number of replicas to be deleted the algorithm
        //   will only consider 'good' chunks (the ones which meet the 'colocation'
        //   requirement and which has good chunks only.
        //
        // - at a presense of more than one candidate for deletion, a worker with
        //   more chunks will be chosen.
        //
        // - the statistics for the number of chunks on each worker will be
        //   updated as deletion requests targeting the corresponding
        //   workers were issued.
        //
        // ATTENTION: the read-only workers will not be considered by
        //            the algorithm. Those workers are used by different kinds
        //            of jobs.
    
        FindAllJobResult const& replicaData = _findAllJob->getReplicaData ();

        // The number of replicas to be deleted for eligible chunks
        //
        std::map<unsigned int,int> chunk2numReplicas2delete;
    
        for (auto const& chunk2workers: replicaData.isGood) {
            unsigned int const  chunk    = chunk2workers.first;
            auto         const& replicas = chunk2workers.second;
    
            size_t const numReplicas = replicas.size();
            if (numReplicas > _numReplicas)
                chunk2numReplicas2delete[chunk] = numReplicas - _numReplicas;
        }

        // The 'occupancy' map or workers which will be used by the replica
        // removal algorithm later. The map is initialized below is based on
        // results reported by the precursor job and it will also be dynamically
        // updated by the algorithm as new replica removal requests for workers will
        // be issued.
        //
        // Note, this map includes chunks in any state.
        //
        std::map<std::string, size_t> worker2occupancy;
    
        for (auto const& chunk2databases     : replicaData.chunks) {
            for (auto const& database2workers: chunk2databases.second) {
                for (auto const& worker2info : database2workers.second) {
                    std::string const& worker = worker2info.first;
                    worker2occupancy[worker]++;
                }
            }
        }
    
        /////////////////////////////////////////////////////////////////////
        // Check which chunks are over-represented. Then find a least loaded
        // worker and launch a replication request.
    
        auto self = shared_from_base<PurgeJob>();
    
        for (auto const& chunk2replicas: chunk2numReplicas2delete) {
    
            unsigned int const chunk              = chunk2replicas.first;
            int                numReplicas2delete = chunk2replicas.second;
    
            // Chunk locking is mandatory. If it's not possible to do this now then
            // the job will need to make another attempt later.
    
            if (not _controller->serviceProvider().chunkLocker().lock({_databaseFamily, chunk}, _id)) {
                ++_numFailedLocks;
                continue;
            }
    
            // This list of workers will be reduced as the replica will get deleted
            std::list<std::string> goodWorkersOfThisChunk;
            for (auto const& entry: replicaData.isGood.at(chunk)) {
                std::string const& worker = entry.first;
                goodWorkersOfThisChunk.push_back(worker);
            }
    
            // Begin shaving extra 'good' replicas of the chunk
    
            for (int i = 0; i < numReplicas2delete; ++i) {
    
                // Find the most populated worker among the good ones of this chunk,
                // which are still available.
    
                size_t      maxNumChunks = 0;   // will get updated witin the next loop
                std::string targetWorker;       // will be set to the best worker inwhen the loop is over
    
                for (auto const& worker: goodWorkersOfThisChunk) {
                    if (targetWorker.empty() or (worker2occupancy[worker] > maxNumChunks)) {
                        maxNumChunks = worker2occupancy[worker];
                        targetWorker = worker;
                    }
                }
                if (targetWorker.empty() or not maxNumChunks) {
                    LOGS(_log, LOG_LVL_ERROR, context() << "onPrecursorJobFinish  "
                         << "failed to find a target worker for chunk: " << chunk);
                    setState (State::FINISHED, ExtendedState::FAILED);
                    break;
                }
    
                // Remove the selct worker from the list, so that the next iteration (if the one
                // will happen) will be not considering this worker fro deletion.
    
                goodWorkersOfThisChunk.remove (targetWorker);
    
                // Finally, launch and register for further tracking deletion
                // requests for all participating databases
    
                for (auto const& database: replicaData.databases.at(chunk)) {
    
                    DeleteRequest::pointer ptr =
                        _controller->deleteReplica (
                            targetWorker,
                            database,
                            chunk,
                            [self] (DeleteRequest::pointer ptr) {
                                self->onRequestFinish(ptr);
                            },
                            0,      /* priority */
                            true,   /* keepTracking */
                            true,   /* allowDuplicate */
                            _id     /* jobId */
                        );
    
                    _chunk2requests[chunk][targetWorker][database] = ptr;
                    _requests.push_back (ptr);
    
                    _numLaunched++;
    
                    // Reduce the worker occupancy count, so that it will be taken into
                    // consideration when creating next replicas.
    
                    worker2occupancy[targetWorker]--;
                }
            }
            if (_state == State::FINISHED) break;
        }
        if (_state == State::FINISHED) break;

        // Finish right away if no problematic chunks found
        if (not _requests.size()) {
            if (not _numFailedLocks) {
                setState (State::FINISHED, ExtendedState::SUCCESS);
                break;
            } else {
                // Some of the chuks were locked and yet, no sigle request was
                // lunched. Hence we should start another iteration by requesting
                // the fresh state of the chunks within the family.
                restart ();
                return;
            }
        }

    } while (false);
    
    // Client notification should be made from the lock-free zone
    // to avoid possible deadlocks
    if (_state == State::FINISHED)
        notify();
}

void
PurgeJob::onRequestFinish (DeleteRequest::pointer request) {

    std::string  const database = request->database(); 
    std::string  const worker   = request->worker(); 
    unsigned int const chunk    = request->chunk();

    LOGS(_log, LOG_LVL_DEBUG, context()
         << "onRequestFinish"
         << "  database=" << database
         << "  worker="   << worker
         << "  chunk="    << chunk);

    do {
        // This lock will be automatically release beyon this scope
        // to allow client notifications (see the end of the method)
        LOCK_GUARD;

        // Ignore the callback if the job was cancelled   
        if (_state == State::FINISHED) {
            release (chunk);
            return;
        }

        // Update counters and object state if needed.

        _numFinished++;
        if (request->extendedState() == Request::ExtendedState::SUCCESS) {
            _numSuccess++;
            _replicaData.replicas.emplace_back(request->responseData());
            _replicaData.chunks[chunk][database][worker] = request->responseData();
            _replicaData.workers[request->worker()] = true;
        } else {
            _replicaData.workers[request->worker()] = false;
        }

        // Make sure the chunk is released if this was the last
        // request in its scope.
        //
        _chunk2requests.at(chunk).at(worker).erase(database);
        if (_chunk2requests.at(chunk).at(worker).empty()) {
            _chunk2requests.at(chunk).erase(worker);
            if (_chunk2requests.at(chunk).empty()) {
                _chunk2requests.erase(chunk);
                release(chunk);
            }
        }
        
        // Evaluate the status of on-going operations to see if the job
        // has finished.
        //
        if (_numFinished == _numLaunched) {
            if (_numSuccess == _numLaunched) {
                if (_numFailedLocks) {
                    // Make another iteration (and another one, etc. as many as needed)
                    // before it succeeds or fails.
                    restart ();
                    return;
                } else {
                    setState (State::FINISHED, ExtendedState::SUCCESS);
                    break;
                }
            } else {
                setState (State::FINISHED, ExtendedState::FAILED);
                break;
            }
        }

    } while (false);

    // Client notification should be made from the lock-free zone
    // to avoid possible deadlocks
    if (_state == State::FINISHED)
        notify ();
}

void
PurgeJob::release (unsigned int chunk) {
    LOGS(_log, LOG_LVL_DEBUG, context() << "release  chunk=" << chunk);
    Chunk chunkObj {_databaseFamily, chunk};
    _controller->serviceProvider().chunkLocker().release(chunkObj);
}

}}} // namespace lsst::qserv::replica_core