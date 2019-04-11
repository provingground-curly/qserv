#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <vector>
#include <stdexcept>
#include <string>

#include "proto/replication.pb.h"
#include "replica/CmdParser.h"
#include "replica/ReplicaFinder.h"
#include "replica_core/Controller.h"
#include "replica_core/ReplicaInfo.h"
#include "replica_core/RequestTracker.h"
#include "replica_core/ReplicationRequest.h"
#include "replica_core/ServiceProvider.h"

namespace r  = lsst::qserv::replica;
namespace rc = lsst::qserv::replica_core;

namespace {

// Command line parameters

std::string  databaseName;
unsigned int numReplicas;
bool         progressReport;
bool         errorReport;
std::string  configUrl;

/// Run the test
bool test () {

    try {

        ///////////////////////////////////////////////////////////////////////
        // Start the controller in its own thread before injecting any requests
        // Note that omFinish callbak which are activated upon a completion
        // of the requsts will be run in that Controller's thread.

        rc::ServiceProvider provider (configUrl);

        rc::Controller::pointer controller = rc::Controller::create (provider);

        controller->run();

        ////////////////////////////////////////
        // Find all replicas accross all workers

        r::ReplicaFinder finder (controller,
                                 databaseName,
                                 std::cout,
                                 progressReport,
                                 errorReport);

        /////////////////////////////////////////////////////////////////
        // Analyse results and prepare a replication plan to create extra
        // replocas for under-represented chunks 
            
        // All workers which have a chunk
        std::map<unsigned int, std::list<std::string>> chunk2workers;

        /// All chunks hosted by a worker
        std::map<std::string, std::list<unsigned int>> worker2chunks;

        // Failed workers
        std::set<std::string> failedWorkers;

        for (const auto& ptr: finder.requests)

            if ((ptr->state()         == rc::Request::State::FINISHED) &&
                (ptr->extendedState() == rc::Request::ExtendedState::SUCCESS)) {

                for (const auto &replica: ptr->responseData ())
                    if (replica.status() == rc::ReplicaInfo::Status::COMPLETE) {
                        chunk2workers[replica.chunk ()].push_back(replica.worker());
                        worker2chunks[replica.worker()].push_back(replica.chunk ());
                    }

            } else{
                failedWorkers.insert(ptr->worker());
            }

        /////////////////////////////////////////////////////////////////////
        // Check which chunks are under-represented. Then find a least loaded
        // worker and launch a replication request.

        rc::CommonRequestTracker<rc::ReplicationRequest> tracker (std::cout,
                                                                  progressReport,
                                                                  errorReport);

        // This counter will be used for optimization purposes as the upper limit for
        // the number of chunks per worker in the load balancing algorithm below.
        const size_t numUniqueChunks = chunk2workers.size();

        for (auto &entry: chunk2workers) {

            const unsigned int chunk{entry.first};

            // Take a copy of the non-modified list of workers with chunk's replicas
            // and cache it here to know which workers are allowed to be used as reliable
            // sources vs chunk2workers[chunk] which will be modified below as new replicas
            // will get created.
            const std::list<std::string> replicas{entry.second};

            // Pick the first worker which has this chunk as the 'sourceWorker'
            // in case if we'll decide to replica the chunk within the loop below
            const std::string &sourceWorker = *(replicas.begin());

            // Note that some chunks may have more replicas than required. In that case
            // the difference would be negative.
            const int numReplicas2create = numReplicas - replicas.size();

            for (int i = 0; i < numReplicas2create; ++i) {

                // Find a candidate worker with the least number of chunks.
                // This worker will be select as the 'destinationWorker' for the new replica.
                //
                // ATTENTION: workers which were previously found as 'failed'
                //            are going to be excluded from the search.

                std::string destinationWorker;
                size_t      numChunksPerDestinationWorker = numUniqueChunks;

                for (const auto &worker: provider.config()->workers()) {

                    // Exclude failed workers

                    if (failedWorkers.count(worker)) continue;

                    // Exclude workers which already have this chunk, or for which
                    // there is an outstanding replication requsts. Both kinds of
                    // replicas are registered in chunk2workers[chunk]

                    if (chunk2workers[chunk].end() == std::find(chunk2workers[chunk].begin(),
                                                                chunk2workers[chunk].end(),
                                                                worker)) {
                        if (worker2chunks[worker].size() < numChunksPerDestinationWorker) {
                            destinationWorker = worker;
                            numChunksPerDestinationWorker = worker2chunks[worker].size();
                        }
                    }
                }
                if (destinationWorker.empty()) {
                    std::cerr << "failed to find the least populated worker for replicating chunk: " << chunk
                        << ", skipping this chunk" << std::endl;
                    break;
                }
                 
                // Register this chunk with the worker to bump the number of chunks per
                // the worker so that this updated stats will be accounted for later as
                // the replication process goes.
                worker2chunks[destinationWorker].push_back(chunk);

                // Also register the worker in the chunk2workers[chunk] to prevent it
                // from being select as the 'destinationWorker' for the same replica
                // in case if more than one replica needs to be created.
                chunk2workers[chunk].push_back(destinationWorker);
                
                // Finally, launch and register for further tracking the replication
                // request.
                
                tracker.add (
                    controller->replicate (
                        destinationWorker,
                        sourceWorker,
                        databaseName,
                        chunk,
                        [&tracker] (rc::ReplicationRequest::pointer ptr) {
                            tracker.onFinish(ptr);
                        }
                    )
                );
            }
        }

        // Wait before all request are finished and report
        // failed requests.

        tracker.track();

        ///////////////////////////////////////////////////
        // Shutdown the controller and join with its thread

        controller->stop();
        controller->join();

    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return true;
}
} /// namespace

int main (int argc, const char* const argv[]) {

    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.

    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // Parse command line parameters
    try {
        r::CmdParser parser (
            argc,
            argv,
            "\n"
            "Usage:\n"
            "  <database> <num-replicas> [--progress-report] [--error-report] [--config=<url>]\n"
            "\n"
            "Parameters:\n"
            "  <database>         - the name of a database to inspect\n"
            "  <num-replicas>     - increase the number of replicas in each chunk to this level\n"
            "\n"
            "Flags and options:\n"
            "  --progress-report  - the flag triggering progress report when executing batches of requests\n"
            "  --error-report     - the flag triggering detailed report on failed requests\n"
            "  --config           - a configuration URL (a configuration file or a set of the database\n"
            "                       connection parameters [ DEFAULT: file:replication.cfg ]\n");

        ::databaseName   = parser.parameter<std::string>(1);
        ::numReplicas    = parser.parameter<int>        (2);
        ::progressReport = parser.flag                  ("progress-report");
        ::errorReport    = parser.flag                  ("error-report");
        ::configUrl      = parser.option   <std::string>("config", "file:replication.cfg");

    } catch (std::exception &ex) {
        return 1;
    }  
    ::test();
    return 0;
}
