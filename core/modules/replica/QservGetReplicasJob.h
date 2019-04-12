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
#ifndef LSST_QSERV_REPLICA_QSERV_GETREPLICASJOB_H
#define LSST_QSERV_REPLICA_QSERV_GETREPLICASJOB_H

// System headers
#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <string>

// Qserv headers
#include "replica/GetReplicasQservMgtRequest.h"
#include "replica/Job.h"
#include "replica/ReplicaInfo.h"
#include "replica/SemanticMaps.h"

// This header declarations
namespace lsst {
namespace qserv {
namespace replica {

/**
 * The structure QservGetReplicasJobResult represents a combined result received
 * from the Qserv worker management services upon a completion of the job.
 */
struct QservGetReplicasJobResult {

    /// Per-worker flags indicating if the corresponding replica retrieval
    /// request succeeded.
    std::map<std::string, bool> workers;

    /// Results grouped by:
    ///
    ///   [worker]
    std::map<std::string, QservReplicaCollection> replicas;

    /// Results grouped by:
    ///
    ///   [chunk][database][worker]
    ///
    /// This structure also reports the use counter for each chunks
    ChunkDatabaseWorkerMap<size_t> useCount;
};

/**
  * Class QservGetReplicasJob represents a tool which will find all replicas
  * of all chunks on all worker nodes.
  */
class QservGetReplicasJob : public Job  {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<QservGetReplicasJob> Ptr;

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
     * @param databaseFamily
     *   name of a database family
     *
     * @param inUseOnly
     *   return replicas which are presently in use
     *
     * @param allWorkers
     *   engage all known workers regardless of their status
     *
     * @param controller
     *   for launching requests
     *
     * @param parentJobId
     *   (optional) identifier of a parent job
     *
     * @param onFinish
     *   (optional) callback function to be called upon a job completion
     *
     * @param options
     *   (optional) job options
     *
     * @return
     *   pointer to the created object
     */
    static Ptr create(std::string const& databaseFamily,
                      bool inUseOnly,
                      bool allWorkers,
                      Controller::Ptr const& controller,
                      std::string const& parentJobId=std::string(),
                      CallbackType const& onFinish=nullptr,
                      Job::Options const& options=defaultOptions());

    // Default construction and copy semantics are prohibited

    QservGetReplicasJob() = delete;
    QservGetReplicasJob(QservGetReplicasJob const&) = delete;
    QservGetReplicasJob& operator=(QservGetReplicasJob const&) = delete;

    ~QservGetReplicasJob() final = default;

    /// @return the name of a database family defining a scope of the operation
    std::string const& databaseFamily() const { return _databaseFamily; }

    /// @return flag indicating (if set) to report a subset of chunks which are in use
    bool inUseOnly() const { return _inUseOnly; }

    /// @return 'true' if all known workers were engaged
    bool allWorkers() const { return _allWorkers; }

    /**
     * @return the result of the operation (when the job finishes)
     *
     * @note
     *   The method should be invoked only after the job has finished (primary
     *   status is set to Job::Status::FINISHED). Otherwise exception
     *   std::logic_error will be thrown
     *
     * @note
     *   The result will be extracted from requests which have successfully
     *   finished. Please, verify the primary and extended status of the object
     *   to ensure that all requests have finished.
     *
     * @throw std::logic_error
     *   if the job didn't finished at a time when the method was called
     */
    QservGetReplicasJobResult const& getReplicaData() const;

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

    /// @see QservGetReplicasJob::create()
    QservGetReplicasJob(std::string const& databaseFamily,
                        bool inUseOnly,
                        bool allWorkers,
                        Controller::Ptr const& controller,
                        std::string const& parentJobId,
                        CallbackType const& onFinish,
                        Job::Options const& options);

    /**
     * The callback function to be invoked on a completion of each request.
     *
     * @param request
     *   a pointer to a request
     */
    void _onRequestFinish(GetReplicasQservMgtRequest::Ptr const& request);


    // Input parameters

    std::string const _databaseFamily;
    bool        const _inUseOnly;
    bool        const _allWorkers;
    CallbackType      _onFinish;    /// @note is reset when the job finishes

    /// A collection of requests implementing the operation
    std::list<GetReplicasQservMgtRequest::Ptr> _requests;

    // The counter of requests which will be updated. They need to be atomic
    // to avoid race condition between the onFinish() callbacks executed within
    // the Controller's thread and this thread.

    std::atomic<size_t> _numLaunched;   ///< the total number of requests launched
    std::atomic<size_t> _numFinished;   ///< the total number of finished requests
    std::atomic<size_t> _numSuccess;    ///< the number of successfully completed requests

    /// The result of the operation (gets updated as requests are finishing)
    QservGetReplicasJobResult _replicaData;
};

}}} // namespace lsst::qserv::replica

#endif // LSST_QSERV_REPLICA_QSERV_GETREPLICASJOB_H
