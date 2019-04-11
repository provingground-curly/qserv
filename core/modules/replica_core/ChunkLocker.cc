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

#include "replica_core/ChunkLocker.h"

// System headers

#include <stdexcept>
#include <tuple>        // std::tie


#define LOCK_GUARD \
std::lock_guard<std::mutex> lock(_mtx)

// Qserv headers

namespace lsst {
namespace qserv {
namespace replica_core {


///////////////////////////////////////
//                Chunk              //
///////////////////////////////////////

bool operator== (Chunk const& lhs,
                 Chunk const& rhs) {
    return  std::tie (lhs.databaseFamily, lhs.number) ==
            std::tie (rhs.databaseFamily, rhs.number);
}

bool operator< (Chunk const& lhs,
                Chunk const& rhs) {
    return  std::tie (lhs.databaseFamily, lhs.number) <
            std::tie (rhs.databaseFamily, rhs.number);
}

std::ostream& operator<< (std::ostream& os, Chunk const& chunk) {
    os  << "Chunk (" << chunk.databaseFamily << ":" << chunk.number << ")";
    return os;
}

/////////////////////////////////////////////
//                ChunkLocker              //
/////////////////////////////////////////////

ChunkLocker::ChunkLocker () {
}

ChunkLocker::~ChunkLocker () {
}

bool
ChunkLocker::isLocked (Chunk const& chunk) const {
    LOCK_GUARD;
    return _chunk2owner.count(chunk);
}

bool
ChunkLocker::isLocked (Chunk const& chunk,
                       std::string& owner) const {

    LOCK_GUARD;

    if (_chunk2owner.count(chunk)) {
        owner = _chunk2owner.at (chunk);
        return true;
    }
    return false;
}

ChunkLocker::ChunksByOwners
ChunkLocker::locked (std::string const& owner) const {

    if (owner.empty()) return _owner2chunks;
    
    ChunksByOwners owner2chunks;
    if (_owner2chunks.count(owner)) owner2chunks[owner] = _owner2chunks.at(owner);

    return owner2chunks;
}

bool
ChunkLocker::lock (Chunk const&       chunk,
                   std::string const& owner) {
    LOCK_GUARD;
    
    if (owner.empty())
        throw std::invalid_argument ("ChunkLocker::lock  empty owner");

    if (_chunk2owner.count (chunk)) {
        return owner == _chunk2owner.at (chunk);
    }
    _chunk2owner [chunk] = owner;
    _owner2chunks[owner].push_back (chunk);

    return true;
}

bool
ChunkLocker::release (Chunk const& chunk) {

    LOCK_GUARD;

    // An owner (if set) will be ignored by the current method
    std::string owner;
    return releaseImpl (chunk, owner);
}

bool
ChunkLocker::release (Chunk const& chunk,
                      std::string& owner) {
    LOCK_GUARD;
    return releaseImpl (chunk, owner);
}

bool
ChunkLocker::releaseImpl (Chunk const& chunk,
                          std::string& owner) {

    if (!_chunk2owner.count (chunk)) return false;

    // Remove the chunk from this map _only_ after getting its owner
    owner = _chunk2owner.at (chunk);
    _chunk2owner.erase (chunk);

    // Remove the chunk from the list of all chunks claimed by that particular
    // owner as well.
    std::list<Chunk>& chunks = _owner2chunks.at (owner);
    chunks.remove (chunk);

    // This last step is needed to avoid building up empty lists
    // of non-existing owners
    if (chunks.empty()) _owner2chunks.erase (owner);

    return true;
}

std::vector<Chunk>
ChunkLocker::release (std::string const& owner) {

    LOCK_GUARD;

    if (owner.empty())
        throw std::invalid_argument ("ChunkLocker::release  empty owner");

    // First get all chunks claimed by the specified owner.
    // This list s also going to be returned o teh cller.
    std::vector<Chunk> chunks;
    if (_owner2chunks.count(owner))
        for (auto const& chunk: _owner2chunks.at(owner))
            chunks.emplace_back(chunk);

    // Then release the select chunks
    for (auto const& chunk: chunks)
        release (chunk);

    return chunks;
}



//////////////////////////////////////
//               misc               //
//////////////////////////////////////

std::ostream& operator << (std::ostream& os, ChunkLocker::ChunksByOwners const& chunks) {
    for (auto const& entry: chunks) {
        std::string const& owner = entry.first;
        os  << "Chunk owner: " << owner << "\n";
        for (Chunk const& chunk: entry.second)
            os  << "    " << chunk.databaseFamily << ":" << chunk.number << "\n";
    }
    return os;
}


}}} // namespace lsst::qserv::replica_core