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
#include "replica_core/DatabaseServices.h"

// System headers

#include <stdexcept>

// Qserv headers

#include "lsst/log/Log.h"
#include "replica_core/Configuration.h"
#include "replica_core/Controller.h"
#include "replica_core/DatabaseServicesMySQL.h"


namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica_core.DatabaseServices");

} /// namespace


namespace lsst {
namespace qserv {
namespace replica_core {

DatabaseServices::pointer
DatabaseServices::create (Configuration& configuration) {

    // If the configuration is pulled from a database then *try*
    // using the corresponding technology.

    if ("mysql" == configuration.databaseTechnology()) {
        try {
            return DatabaseServices::pointer (
                new DatabaseServicesMySQL (configuration));
        } catch (database::mysql::Error const& ex) {
            LOGS(_log, LOG_LVL_ERROR, "DatabaseServices::  failed to instantiate MySQL-based database services" <<
                                      ", error: " << ex.what() <<
                                      ", no such service will be available to the application.");
        }
    }

    // Otherwise assume the current 'dummy' implementation.
    return DatabaseServices::pointer (
            new DatabaseServices (configuration));
}

DatabaseServices::DatabaseServices (Configuration& configuration)
    :   _configuration (configuration) {
}

DatabaseServices::~DatabaseServices () {
}

void
DatabaseServices::saveControllerState (ControllerIdentity const& identity,
                                       uint64_t                  startTime) {

    LOGS(_log, LOG_LVL_DEBUG, "DatabaseServices::saveControllerState");
}

void
DatabaseServices::saveJobState (Job_pointer const& job) {
    LOGS(_log, LOG_LVL_DEBUG, "DatabaseServices::saveJobState");
}

}}} // namespace lsst::qserv::replica_core