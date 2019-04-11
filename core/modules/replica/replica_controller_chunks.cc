#include <atomic>
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>
#include <stdexcept>
#include <string>
#include <set>

#include "proto/replication.pb.h"
#include "replica/CmdParser.h"
#include "replica/ReplicaFinder.h"
#include "replica_core/Controller.h"
#include "replica_core/FindAllRequest.h"
#include "replica_core/ReplicaInfo.h"
#include "replica_core/ServiceProvider.h"

namespace r  = lsst::qserv::replica;
namespace rc = lsst::qserv::replica_core;

namespace {

// Command line parameters

std::string databaseName;
bool        progressReport;
bool        errorReport;
std::string configUrl;

/// Run the test
bool test () {

    try {

        ///////////////////////////////////////////////////////////////////////
        // Start the controller in its own thread before injecting any requests
        // Note that omFinish callbak which are activated upon a completion
        // of the requsts will be run in that Controller's thread.

        rc::ServiceProvider provider (configUrl);

        rc::Controller::pointer controller = rc::Controller::create(provider);

        controller->run();

        ////////////////////////////////////////
        // Find all replicas accross all workers

        r::ReplicaFinder finder (controller,
                                 databaseName,
                                 std::cout,
                                 progressReport,
                                 errorReport);

        //////////////////////////////
        // Analyse and display results

        std::cout
            << "\n"
            << "WORKERS:";
        for (const auto &worker: provider.config()->workers()) {
            std::cout << " " << worker;
        }
        std::cout
            << std::endl;

        // Workers hosting a chunk
        std::map<unsigned int, std::vector<std::string>> chunk2workers;

        // Chunks hosted by a worker
        std::map<std::string, std::vector<unsigned int>> worker2chunks;

        // Failed workers
        std::set<std::string> failedWorkers;

        for (const auto &ptr: finder.requests)

            if ((ptr->state()         == rc::Request::State::FINISHED) &&
                (ptr->extendedState() == rc::Request::ExtendedState::SUCCESS))

                for (const auto &replica: ptr->responseData ()) {
                    chunk2workers[replica.chunk()].push_back (
                        replica.worker() + (replica.status() == rc::ReplicaInfo::Status::COMPLETE ? "" : "(!)"));
                    worker2chunks[replica.worker()].push_back(replica.chunk());
                }
            else
                failedWorkers.insert(ptr->worker());

        std::cout
            << "\n"
            << "CHUNK DISTRIBUTION:\n"
            << "----------+------------\n"
            << "   worker | num.chunks \n"
            << "----------+------------\n";

        for (const auto &worker: provider.config()->workers())
            std::cout
                << " " << std::setw(8) << worker << " | " << std::setw(10)
                << (failedWorkers.count(worker) ? "*" : std::to_string(worker2chunks[worker].size())) << "\n";

        std::cout
            << "----------+------------\n"
            << std::endl;

        std::cout
            << "REPLICAS:\n"
            << "----------+--------------+---------------------------------------------\n"
            << "    chunk | num.replicas | worker(s)  \n"
            << "----------+--------------+---------------------------------------------\n";

        for (const auto &entry: chunk2workers) {
            const auto &chunk    = entry.first;
            const auto &replicas = entry.second;
            std::cout
                << " " << std::setw(8) << chunk << " | " << std::setw(12) << replicas.size() << " |";
            for (const auto &replica: replicas) {
                std::cout << " " << replica;
            }
            std::cout << "\n";
        }
        std::cout
            << "----------+--------------+---------------------------------------------\n"
            << std::endl;


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
            "  <database> [--progress-report] [--error-report] [--config=<url>]\n"
            "\n"
            "Parameters:\n"
            "  <database>         - the name of a database to inspect\n"
            "\n"
            "Flags and options:\n"
            "  --progress-report  - the flag triggering progress report when executing batches of requests\n"
            "  --error-report     - the flag triggering detailed report on failed requests\n"
            "  --config           - a configuration URL (a configuration file or a set of the database\n"
            "                       connection parameters [ DEFAULT: file:replication.cfg ]\n");

        ::databaseName   = parser.parameter<std::string>(1);
        ::progressReport = parser.flag                  ("progress-report");
        ::errorReport    = parser.flag                  ("error-report");
        ::configUrl      = parser.option   <std::string>("config", "file:replication.cfg");

    } catch (std::exception &ex) {
        return 1;
    } 
    ::test();
    return 0;
}