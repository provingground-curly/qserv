#include <iostream>
#include <string>
#include <thread>

#include "lsst/log/Log.h"
#include "proto/replication.pb.h"
#include "replica/CmdParser.h"
#include "replica_core/BlockPost.h"
#include "replica_core/ServiceProvider.h"
#include "replica_core/FileServer.h"

namespace r  = lsst::qserv::replica;
namespace rc = lsst::qserv::replica_core;

namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica.replica_file_server");

// Command line parameters

std::string workerName;
std::string configUrl;

/**
 * Instantiate and launch the service in its own thread. Then block
 * the current thread in a series of repeated timeouts.
 */
void service () {
    
    try {
        rc::ServiceProvider provider (configUrl);

        rc::FileServer::pointer server =
            rc::FileServer::create (provider, workerName);

        std::thread serverLauncherThread ([server]() {
            server->run();
        });
        rc::BlockPost blockPost (1000, 5000);
        while (true) {
            blockPost.wait();
            LOGS(_log, LOG_LVL_INFO, "HEARTBEAT  worker: " << server->worker());
        }
        serverLauncherThread.join();

    } catch (std::exception& e) {
        LOGS(_log, LOG_LVL_ERROR, e.what());
    }
}
}  /// namespace

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
            "  <worker> [--config=<url>]\n"
            "\n"
            "Parameters:\n"
            "  <worker>   - the name of a worker\n"
            "\n"
            "Flags and options:\n"
            "  --config   - a configuration URL (a configuration file or a set of the database\n"
            "               connection parameters [ DEFAULT: file:replication.cfg ]\n");

        ::workerName = parser.parameter<std::string> (1);
        ::configUrl  = parser.option   <std::string> ("config", "file:replication.cfg");

    } catch (std::exception const& ex) {
        return 1;
    } 
    ::service ();
    return 0;
}
