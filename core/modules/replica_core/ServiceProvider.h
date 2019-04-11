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
#ifndef LSST_QSERV_REPLICA_CORE_SERVICEPROVIDER_H
#define LSST_QSERV_REPLICA_CORE_SERVICEPROVIDER_H

/// ServiceProvider.h declares:
///
/// class ServiceProvider
/// (see individual class documentation for more information)

// System headers

#include <string>
#include <vector>

// Qserv headers

#include "replica_core/ChunkLocker.h"
#include "replica_core/Configuration.h"
#include "replica_core/DatabaseServices.h"

// Forward declarations

// This header declarations

namespace lsst {
namespace qserv {
namespace replica_core {

/**
  * Class ServiceProvider hosts various serviceses for the master server.
  */
class ServiceProvider {

public:

    // Default construction and copy semantics are proxibited

    ServiceProvider () = delete;
    ServiceProvider (ServiceProvider const&) = delete;
    ServiceProvider& operator= (ServiceProvider const&) = delete;

    /**
     * Construct the object.
     *
     * @param configUrl - a source of the application configuration parameters
     */
    explicit ServiceProvider (std::string const& configUrl);

    /**
     * Return a reference to the configuration service
     */
    Configuration::pointer const& config () const { return _configuration; }

    /**
     * Return a reference to the database services
     */
    DatabaseServices::pointer const& databaseServices () const { return _databaseServices; }

    ChunkLocker& chunkLocker () { return _chunkLocker; }

    /**
     * Make sure this worker is known in the configuration. Throws exception
     * std::invalid_argument otherwise.
     * 
     * @param name - the name of a worker
     */
    void assertWorkerIsValid (std::string const& name);

    /**
     * Make sure workers are now known in the configuration and they're different.
     * Throws exception std::invalid_argument otherwise.
     */
    void assertWorkersAreDifferent (std::string const& workerOneName,
                                    std::string const& workerTwoName);

    /**
     * Make sure this database is known in the configuration. Throws exception
     * std::invalid_argument otherwise.
     *
     * @param name - the name of a database
     */
    void assertDatabaseIsValid (std::string const& name);

private:

    /// Configuration manager
    Configuration::pointer _configuration;

    /// Database services
    DatabaseServices::pointer _databaseServices;

    /// For claiming exclusive ownership over chunks during replication
    /// operations to ensure consistency of the operations.
    ChunkLocker _chunkLocker;
};

}}} // namespace lsst::qserv::replica_core

#endif // LSST_QSERV_REPLICA_CORE_SERVICEPROVIDER_H