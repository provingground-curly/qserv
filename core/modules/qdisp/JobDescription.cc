/*
 * LSST Data Management System
 * Copyright 2014-2015 AURA/LSST.
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
 *
 * JobDescription.cc
 *      Author: jgates
 */

// Class header
#include "qdisp/JobDescription.h"

// System headers
#include <sstream>

namespace lsst {
namespace qserv {
namespace qdisp {

std::string JobDescription::toString() const {
    std::ostringstream os;
    os << *this;
    return os.str();
}

std::ostream& operator<<(std::ostream& os, JobDescription const& jd) {
    os << "job(id=" << jd._id << " payload.len=" << jd._payload.length()
       << " ru=" << jd._resource.path() <<  ")";
    return os;
}

}}} // namespace
