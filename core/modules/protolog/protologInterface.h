// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2013 LSST Corporation.
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
/// protologInterface.h declares an interface for the logging mechanism for exporting
/// via SWIG to the python layer of Qserv.

#ifndef LSST_QSERV_MASTER_PROTOLOGINTERFACE_H
#define LSST_QSERV_MASTER_PROTOLOGINTERFACE_H

namespace lsst { namespace qserv { namespace master {

void initLog_iface(std::string const& filename);
std::string getDefaultLoggerName_iface(void);
void pushContext_iface(std::string const& name);
void popContext_iface();
void MDC_iface(std::string const& key, std::string const& value);
void MDCRemove_iface(std::string const& key);
void setLevel_iface(std::string const& loggername, int level);
int getLevel_iface(std::string const& loggername);
bool isEnabledFor_iface(std::string const& loggername, int level);
void forcedLog_iface(std::string const& loggername, int level,
                     std::string const& filename, std::string const& funcname,
                     int lineno, std::string const& mdg);

}}} // namespace lsst::qserv::master

#endif // LSST_QSERV_MASTER_PROTOLOGINTERFACE_H