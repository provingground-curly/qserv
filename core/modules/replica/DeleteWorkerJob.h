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
#ifndef LSST_QSERV_REPLICA_DELETEWORKERJOB_H
#define LSST_QSERV_REPLICA_DELETEWORKERJOB_H

// System headers
#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <string>

// Qserv headers
#include "replica/Job.h"
#include "replica/FindAllRequest.h"
#include "replica/ReplicaInfo.h"
#include "replica/ReplicateJob.h"

// This header declarations
namespace lsst {
namespace qserv {
namespace replica {

/**
 * The structure DeleteWorkerJobResult represents a combined result received
 * from worker services upon a completion of the job.
 */
struct DeleteWorkerJobResult {

    /// New replicas created upon successful completion of the request
    FamilyChunkDatabaseWorkerInfo chunks;

    /// Completely lost replicas which only existed on the deleted worker node
    ChunkDatabaseReplicaInfo orphanChunks;
};

/**
  * Class DeleteWorkerJob represents a tool which will disable a worker
  * from any active use in a replication setup. All chunks hosted by
  * the worker node will be distributed across the cluster.
  *
  * Specific steps made by the job:
  * 1. check the status of the worker service (SYNC, short timeout),
  *    1.1 if it responds then:
  *        1.1.0 memorize the status of the worker service (may need it later)
  *        1.1.1 drain all requests on that service (SYNC, short timeout)
  *        1.1.2 submit FindAllRequsts against the worker for all known databases
  *              to refresh a list of replicas on that node (ASYNC)
  *        1.1.3 when all requests finish then stop the worker service (SYNC, short timeout)
  *        1.1.4 proceed to step 2
  *     1.2 otherwise:
  *        1.2.1 proceed to step 2
  * 2. change the status of the worker
  *    2.1 disable worker in the configuration
  *    2.2 update a disposition of chunks across the rest of the cluster (ASYNC)
  * 3. launch the replication job ReplicateJob (ASYNC, long timeout)
  * 4. analyze results when the job will finish a report on which replicas were
  *    made and which could not be made will be prepared. See structure DeleteWorkerJobResult
  *    defined above for specific details.
  *    4.1 load a list of affected worker's replicas from the database
  *    4.2 if such orphans found check 1.0 to see if the worker service could
  *        be reactivated to pull those missing replicas from that node
  *        TBC...
  */
class DeleteWorkerJob : public Job  {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<DeleteWorkerJob> Ptr;

    /// The function type for notifications on the completion of the request
    typedef std::function<void(Ptr)> CallbackType;

    /// @return default options object for this type of a request
    static Job::Options const& defaultOptions();

    /// @return the unique name distinguishing this class from other types of jobs
    static std::string typeName();

    /**
     * Static factory method is needed to prevent issue with the lifespan
     * and memory management of instances created otherwise (as values or via
     * low-level pointers).
     *
     * @param worker
     *   the name of a worker to be deleted
     *
     * @param permanentDelete
     *   if set to 'true' the worker record will be completely
     *   wiped out from the configuration
     *
     * @param controller
     *   for launching requests
     *
     * @param parentJobId
     *   optional identifier of a parent job
     *
     * @param onFinish
     *   optional callback function to be called upon a completion of the job
     *
     * @param options
     *   (optional) job options
     */
    static Ptr create(std::string const& worker,
                      bool permanentDelete,
                      Controller::Ptr const& controller,
                      std::string const& parentJobId=std::string(),
                      CallbackType const& onFinish=nullptr,
                      Job::Options const& options=defaultOptions());

    // Default construction and copy semantics are prohibited

    DeleteWorkerJob() = delete;
    DeleteWorkerJob(DeleteWorkerJob const&) = delete;
    DeleteWorkerJob& operator=(DeleteWorkerJob const&) = delete;

    ~DeleteWorkerJob() final = default;

    /// @return the name of a worker to be deleted
    std::string const& worker() const { return _worker; }

    /// @return 'true' if this is the permanent delete
    bool permanentDelete() const { return _permanentDelete; }

    /**
     * Return the result of the operation.
     *
     * @note:
     *  The method should be invoked only after the job has finished (primary
     *  status is set to Job::Status::FINISHED). Otherwise exception
     *  std::logic_error will be thrown
     *
     * @note
     *  The result will be extracted from requests which have successfully
     *  finished. Please, verify the primary and extended status of the object
     *  to ensure that all requests have finished.
     *
     * @return
     *   the data structure to be filled upon the completion of the job.
     *
     * @throws std::logic_error
     *   if the job didn't finished at a time when the method was called
     */
    DeleteWorkerJobResult const& getReplicaData() const;

    /// @see Job::extendedPersistentState()
    std::list<std::pair<std::string,std::string>> extendedPersistentState() const final;

    /// @see Job::persistentLogData()
    std::list<std::pair<std::string,std::string>> persistentLogData() const final;

protected:

    /// @see Job::startImpl()
    void startImpl(util::Lock const& lock) final;

    /// @see Job::cancelImpl()
    void cancelImpl(util::Lock const& lock) final;

    /// @see Job::notify()
    void notify(util::Lock const& lock) final;

private:

    /// @see DeleteWorkerJob::create()
    DeleteWorkerJob(std::string const& worker,
                    bool permanentDelete,
                    Controller::Ptr const& controller,
                    std::string const& parentJobId,
                    CallbackType const& onFinish,
                    Job::Options const& options);

    /**
     * Begin the actual sequence of actions for removing the worker
     *
     * @param lock
     *   a lock on Job::_mtx must be acquired before calling this method
     */
    void _disableWorker(util::Lock const& lock);

    /**
     * The callback function to be invoked on a completion of each request.
     *
     * @param request
     *   a pointer to a request
     */
    void _onRequestFinish(FindAllRequest::Ptr const& request);

    /**
     * The callback function to be invoked on a completion of a job
     * which ensures the desired replication level after disabling .
     *
     * @param request
     *   a pointer to a job
     */
    void _onJobFinish(ReplicateJob::Ptr const& job);


    // Input parameters

    std::string const _worker;
    bool        const _permanentDelete;
    CallbackType      _onFinish;        /// @note is reset when the job finishes

    // The counter of requests/jobs which will be updated. They need to be atomic
    // to avoid race condition between the onFinish() callbacks executed within
    // the Controller's thread and this thread.

    std::atomic<size_t> _numLaunched;   ///< the total number of requests launched
    std::atomic<size_t> _numFinished;   ///< the total number of finished requests
    std::atomic<size_t> _numSuccess;    ///< the number of successfully completed requests

    /// A collection of requests (one per a database) to be launched against
    /// the affected worker in order to get the latest state of the worker's
    /// replicas
    std::list<FindAllRequest::Ptr> _findAllRequests;

    /// The chained jobs (one per each database family which are launched after
    /// disabling the worker in order to ensure the minimum replication level
    /// across the replication setup
    std::list<ReplicateJob::Ptr> _replicateJobs;

    /// The result of the operation (gets updated as requests are finishing)
    DeleteWorkerJobResult _replicaData;
};

}}} // namespace lsst::qserv::replica

#endif // LSST_QSERV_REPLICA_DELETEWORKERJOB_H
