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
#ifndef LSST_QSERV_REPLICA_CORE_DELETE_WORKER_JOB_H
#define LSST_QSERV_REPLICA_CORE_DELETE_WORKER_JOB_H

/// DeleteWorkerJob.h declares:
///
/// struct DeleteWorkerJobResult
/// class  DeleteWorkerJob
///
/// (see individual class documentation for more information)

// System headers

#include <atomic>
#include <functional>   // std::function
#include <list>
#include <map>
#include <string>

// Qserv headers

#include "replica_core/Job.h"
#include "replica_core/FindAllJob.h"
#include "replica_core/FindAllRequest.h"
#include "replica_core/ReplicaInfo.h"
#include "replica_core/ReplicateJob.h"


// Forward declarations

// This header declarations

namespace lsst {
namespace qserv {
namespace replica_core {

/**
 * The structure DeleteWorkerJobResult represents a combined result received
 * from worker services upon a completion of the job.
 */
struct DeleteWorkerJobResult {

    /// New replicas created upon the successfull completion
    /// of the corresponidng requests
    std::map<std::string,               // database family
        std::map<unsigned int,          // chunk
            std::map<std::string,       // database
                std::map<std::string,   // worker
                    ReplicaInfo>>>> chunks;

    /// Replicas which only existed on the deleted worker node and which
    /// couldn't be redistributed
    std::map<unsigned int,              // chunk
        std::map<std::string,           // database
            ReplicaInfo>> orphanChunks;
};

/**
  * Class DeleteWorkerJob represents a tool which will disable a worker
  * from any active use in a replication setup. All chunks hosted by
  * the worker node will be distributed accross the cluster.
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
  *    2.2 update a disposition of chunks accross the rest of the cluster (ASYNC)
  * 3. launch the replication job ReplicateJob (ASYNC, long timeout)
  * 4. analyze results when the job will finish a report on which replicas were
  *    made and which could not be made will be prepared. See struct DeleteWorkerJobResult
  *    defined above for specific details.
  *    4.1 load a list of affected worker's replicas from the database
  *    4.2 if such orpgans found check 1.0 to see if the worker service could
  *        be reactivated to pull those missing replicas from that node
  *        TBC...
  */
class DeleteWorkerJob
    :   public Job  {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<DeleteWorkerJob> pointer;

    /// The function type for notifications on the completon of the request
    typedef std::function<void(pointer)> callback_type;

    /**
     * Static factory method is needed to prevent issue with the lifespan
     * and memory management of instances created otherwise (as values or via
     * low-level pointers).
     *
     * @param worker          - the name of a worker to be deleted
     * @param permanentDelete - if set to 'true' the worker record will be completelly wiped out
     *                          from the configuration
     * @param controller      - for launching requests
     * @param onFinish        - a callback function to be called upon a completion of the job
     * @param bestEffort      - the flag (if set) allowing to proceed with the replication effort
     *                          when some workers fail to report their cunk disposition.
     *                          ATTENTION: do *NOT* use this in production!
     * @param priority        - set the desired job priority (larger values
     *                          mean higher priorities). A job with the highest
     *                          priority will be select from an input queue by
     *                          the JobScheduler.
     * @param exclusive       - set to 'true' to indicate that the job can't be
     *                          running simultaneously alongside other jobs.
     * @param preemptable     - set to 'true' to indicate that this job can be
     *                          interrupted to give a way to some other job of
     *                          high importancy.
     */
    static pointer create (std::string const&         worker,
                           bool                       permanentDelete,
                           Controller::pointer const& controller,
                           callback_type              onFinish,
                           bool                       bestEffort  = false,
                           int                        priority    = 2,
                           bool                       exclusive   = true,
                           bool                       preemptable = false);

    // Default construction and copy semantics are prohibited

    DeleteWorkerJob () = delete;
    DeleteWorkerJob (DeleteWorkerJob const&) = delete;
    DeleteWorkerJob& operator= (DeleteWorkerJob const&) = delete;

    /// Destructor
    ~DeleteWorkerJob () override;

    /// Return the name of a worker to be deleted
    std::string const& worker () const { return _worker; }

    /// Return 'true' i fthis is teh permanent delete
    bool permanentDelete () const { return _permanentDelete; }

    /**
     * Return the result of the operation.
     *
     * IMPORTANT NOTES:
     * - the method should be invoked only after the job has finished (primary
     *   status is set to Job::Status::FINISHED). Otherwise exception
     *   std::logic_error will be thrown
     * 
     * - the result will be extracted from requests which have successfully
     *   finished. Please, verify the primary and extended status of the object
     *   to ensure that all requests have finished.
     *
     * @return the data structure to be filled upon the completin of the job.
     *
     * @throws std::logic_error - if the job dodn't finished at a time
     *                            when the method was called
     */
    DeleteWorkerJobResult const& getReplicaData () const;

    /**
      * Implement the corresponding method of the base class.
      *
      * @see Job::track()
      */
    void track (bool          progressReport,
                bool          errorReport,
                bool          chunkLocksReport,
                std::ostream& os) const override;

protected:

    /**
     * Construct the job with the pointer to the services provider.
     *
     * @see DeleteWorkerJob::create()
     */
    DeleteWorkerJob (std::string const&         worker,
                     bool                       permanentDelete,
                     Controller::pointer const& controller,
                     callback_type              onFinish,
                     bool                       bestEffort,
                     int                        priority,
                     bool                       exclusive,
                     bool                       preemptable);

    /**
      * Implement the corresponding method of the base class.
      *
      * @see Job::startImpl()
      */
    void startImpl () override;

    /**
      * Implement the corresponding method of the base class.
      *
      * @see Job::startImpl()
      */
    void cancelImpl () override;

    /**
      * Implement the corresponding method of the base class.
      *
      * @see Job::notify()
      */
    void notify () override;

    /**
     * Beging the actual sequence of actions for removing the worker
     */
    void disableWorker ();
    
    /**
     * The calback function to be invoked on a completion of each request.
     *
     * @param request - a pointer to a request
     */
    void onRequestFinish (FindAllRequest::pointer request);

    /**
     * The calback function to be invoked on a completion of a job
     * which harvests chunk disposition accross relevant worker nodes.
     * 
     * @param request - a pointer to a job
     */
    void onJobFinish (FindAllJob::pointer job);

    /**
     * The calback function to be invoked on a completion of a job
     * which ensures the desired replication level after disabling .
     * 
     * @param request - a pointer to a job
     */
    void onJobFinish (ReplicateJob::pointer job);

protected:

    /// The name of a worker to be disabled
    std::string _worker;

    /// Permamently remove from the configuration if set
    bool _permanentDelete;

    /// Client-defined function to be called upon the completion of the job
    callback_type _onFinish;

    /// The flag (if set) allowing to proceed with the effort even after
    /// not getting response on chunk disposition from all workers.
    bool _bestEffort;

    // The counter of requests/jobs which will be updated. They need to be atomic
    // to avoid race condition between the onFinish() callbacks executed within
    // the Controller's thread and this thread.

    std::atomic<size_t> _numLaunched;   ///< the total number of requests launched
    std::atomic<size_t> _numFinished;   ///< the total number of finished requests
    std::atomic<size_t> _numSuccess;    ///< the number of successfully completed requests

    /// A collection of requests (one per a database) to be launched against
    /// the affected worker in order to get the latest state of the worker's
    /// replicas
    std::list<FindAllRequest::pointer> _findAllRequests;

    /// The chained jobs (one per database family) needed to refresh replica
    /// disposition accross a replication setup
    std::list<FindAllJob::pointer> _findAllJobs;

    /// The chained jobs (one per each database family which are launched after
    /// disabling the worker in order to ensure the minimum replication level
    /// across the replication setup
    std::list<ReplicateJob::pointer> _replicateJobs;

    /// The result of the operation (gets updated as requests are finishing)
    DeleteWorkerJobResult _replicaData;
};

}}} // namespace lsst::qserv::replica_core

#endif // LSST_QSERV_REPLICA_CORE_DELETE_WORKER_JOB_H