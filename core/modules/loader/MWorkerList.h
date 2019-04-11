// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2018 LSST Corporation.
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
 */
#ifndef LSST_QSERV_LOADER_MWORKERLIST_H_
#define LSST_QSERV_LOADER_MWORKERLIST_H_

// system headers
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

// Qserv headers
#include "loader/Updateable.h"
#include "loader/BufferUdp.h"
#include "loader/DoList.h"
#include "loader/NetworkAddress.h"
#include "loader/StringRange.h"


namespace lsst {
namespace qserv {
namespace loader {

class Central;
class CentralWorker;
class CentralMaster;
class LoaderMsg;


// &&& move this declaration
struct NeighborsInfo {
    NeighborsInfo() = default;
    NeighborsInfo(NeighborsInfo const&) = delete;
    NeighborsInfo& operator=(NeighborsInfo const&) = delete;

    typedef std::shared_ptr<Updatable<uint32_t>> NeighborPtr;
    typedef std::weak_ptr<Updatable<uint32_t>> NeighborWPtr;
    NeighborPtr neighborLeft{new Updatable<uint32_t>(0)};   ///< Neighbor with lesser values
    NeighborPtr neighborRight{new Updatable<uint32_t>(0)};  ///< Neighbor with higher values
    uint32_t recentAdds{0}; ///< Number of keys added to this worker recently.
    uint32_t keyCount{0};   ///< Total number of keys stored on the worker.
};





/// Standard information for a single worker, IP address, key range, timeouts.
class MWorkerListItem : public std::enable_shared_from_this<MWorkerListItem> {
public:
    using Ptr = std::shared_ptr<MWorkerListItem>;
    using WPtr = std::weak_ptr<MWorkerListItem>;

    static MWorkerListItem::Ptr create(uint32_t name, CentralMaster *central) {
        return MWorkerListItem::Ptr(new MWorkerListItem(name, central));
    }
    static MWorkerListItem::Ptr create(uint32_t name, NetworkAddress const& address, CentralMaster *central) {
        return MWorkerListItem::Ptr(new MWorkerListItem(name, address, central));
    }


    MWorkerListItem() = delete;
    MWorkerListItem(MWorkerListItem const&) = delete;
    MWorkerListItem& operator=(MWorkerListItem const&) = delete;

    virtual ~MWorkerListItem() = default;

    NetworkAddress getAddress() const {
        std::lock_guard<std::mutex> lck(_mtx);
        return *_address;
    }
    uint32_t getName() const {
        // std::lock_guard<std::mutex> lck(_mtx); &&&
        return _name;
    }
    StringRange getRangeString() const {
        std::lock_guard<std::mutex> lck(_mtx);
        return _range;
    }

    bool isActive() const { return _active; }

    void addDoListItems(Central *central);

    void setRangeStr(StringRange const& strRange);
    void setAllInclusiveRange();

    void setKeyCounts(NeighborsInfo const& nInfo);
    int  getKeyCount() const;

    void setRightNeighbor(MWorkerListItem::Ptr const& item);
    void setLeftNeighbor(MWorkerListItem::Ptr const& item);

    void flagNeedToSendList();

    util::CommandTracked::Ptr createCommandMaster(CentralMaster* centralM);

    void sendListToWorkerInfoReceived();

    friend std::ostream& operator<<(std::ostream& os, MWorkerListItem const& item);
private:
    MWorkerListItem(uint32_t name, CentralMaster* central) : _name(name), _central(central) {}
    MWorkerListItem(uint32_t name, NetworkAddress const& address, CentralMaster* central)
         : _name(name), _address(new NetworkAddress(address)), _central(central) {}

    uint32_t _name;
    NetworkAddress::UPtr _address{new NetworkAddress("", 0)}; ///< empty string indicates address is not valid.
    TimeOut _lastContact{std::chrono::minutes(10)};  ///< Last time information was received from this worker
    StringRange _range;       ///< min and max range for this worker.
    NeighborsInfo _neighborsInfo; ///< information used to set neighbors.
    mutable std::mutex _mtx;  ///< protects _name, _address, _range

    std::atomic<bool> _active{false}; ///< true when worker has been given a valid range, or a neighbor.

    CentralMaster* _central;

    // Occasionally send a list of all workers to the worker represented by this object.
    struct SendListToWorker : public DoListItem {
        SendListToWorker(MWorkerListItem::Ptr const& mWorkerListItem_, CentralMaster *central_) :
            mWorkerListItem(mWorkerListItem_), central(central_) {}
        MWorkerListItem::WPtr mWorkerListItem;
        CentralMaster *central;
        util::CommandTracked::Ptr createCommand() override;
    };
    DoListItem::Ptr _sendListToWorker;

    // Occasionally ask this worker for information about its list of keys, if it hasn't
    // been heard from.
    struct ReqWorkerKeyInfo : public DoListItem {
        ReqWorkerKeyInfo(MWorkerListItem::Ptr const& mWorkerListItem_, CentralMaster *central_) :
            mWorkerListItem(mWorkerListItem_), central(central_) {}
        MWorkerListItem::WPtr mWorkerListItem;
        CentralMaster *central;
        util::CommandTracked::Ptr createCommand() override;
    };
    DoListItem::Ptr _reqWorkerKeyInfo;
    std::mutex _doListItemsMtx; ///< protects _sendListToWorker


    /* &&&
    /// Create commands to set a worker's neighbor.
    /// It should keep trying this until it works. When the worker sets the neighbor to
    /// the target value, this object should initiate a chain reaction that destorys itself.
    /// It is very important that the message and neighborPtr both point to
    //  the same (left or right) neighbor.
    class SetNeighborOneShot : public DoListItem, public UpdateNotify<uint32_t> {
    public:
        using Ptr = std::shared_ptr<SetNeighborOneShot>;

        SetNeighborOneShot(CentralMaster* central_,
                           MWorkerListItem::Ptr const& target_,
                           int msg_,
                           uint32_t neighborName_,
                           NeighborsInfo::NeighborPtr const& neighborPtr_) :
              central(central_), target(target_), message(msg_), neighborName(neighborName_),
              neighborPtr(neighborPtr_) {
            _oneShot = true;
            auto oneShotPtr = std::static_pointer_cast<SetNeighborOneShot>(getDoListItemPtr());
            auto updatePtr = std::static_pointer_cast<UpdateNotify<uint32_t>>(oneShotPtr);
            neighborPtr_->registerNotify(updatePtr); // Must do this so it will call our updateNotify().
        }

        util::CommandTracked::Ptr createCommand() override;

        // This is called every time the worker sends the master a value for its (left/right) neighbor.
        // See neighborPtr_->registerNotify()
        void updateNotify(uint32_t& oldVal, uint32_t& newVal) override {
            if (newVal == neighborName) {
                infoReceived(); // This should result in this oneShot DoListItem being removed->destroyed.
            }
        }

        CentralMaster* const central;
        MWorkerListItem::WPtr target;
        int const message;
        uint32_t const neighborName;
        NeighborsInfo::NeighborWPtr neighborPtr;

    };
    */
};


/// Create commands to set a worker's neighbor.
/// It should keep trying this until it works. When the worker sets the neighbor to
/// the target value, this object should initiate a chain reaction that destorys itself.
/// It is very important that the message and neighborPtr both point to
//  the same (left or right) neighbor.
class SetNeighborOneShot : public DoListItem, public UpdateNotify<uint32_t> {
public:
    using Ptr = std::shared_ptr<SetNeighborOneShot>;

    SetNeighborOneShot(CentralMaster* central_,
                       MWorkerListItem::Ptr const& target_,
                       int msg_,
                       uint32_t neighborName_,
                       NeighborsInfo::NeighborPtr const& neighborPtr_) :
                central(central_), target(target_), message(msg_), neighborName(neighborName_),
                neighborPtr(neighborPtr_) {
        _oneShot = true;
        auto oneShotPtr = std::static_pointer_cast<SetNeighborOneShot>(getDoListItemPtr());
        auto updatePtr = std::static_pointer_cast<UpdateNotify<uint32_t>>(oneShotPtr);
        neighborPtr_->registerNotify(updatePtr); // Must do this so it will call our updateNotify().
    }

    util::CommandTracked::Ptr createCommand() override;

    // This is called every time the worker sends the master a value for its (left/right) neighbor.
    // See neighborPtr_->registerNotify()
    void updateNotify(uint32_t& oldVal, uint32_t& newVal) override {
        if (newVal == neighborName) {
            infoReceived(); // This should result in this oneShot DoListItem being removed->destroyed.
        }
    }

    CentralMaster* const central;
    MWorkerListItem::WPtr target;
    int const message;
    uint32_t const neighborName;
    NeighborsInfo::NeighborWPtr neighborPtr;

};



class MWorkerList : public DoListItem {
public:
    using Ptr = std::shared_ptr<MWorkerList>;

    MWorkerList(CentralMaster* central) : _central(central) {}
    MWorkerList() = delete;
    MWorkerList(MWorkerList const&) = delete;
    MWorkerList& operator=(MWorkerList const&) = delete;

    virtual ~MWorkerList() = default;

    ///// Master only //////////////////////
    // Returns pointer to new item if an item was created.
    MWorkerListItem::Ptr addWorker(std::string const& ip, int port);

    // Returns true of message could be parsed and a send will be attempted.
    bool sendListTo(uint64_t msgId, std::string const& ip, short port,
                    std::string const& outHostName, short ourPort);

    util::CommandTracked::Ptr createCommand() override;
    util::CommandTracked::Ptr createCommandMaster(CentralMaster* centralM);

    //////////////////////////////////////////
    /// Nearly the same on Worker and Master
    size_t getNameMapSize() {
        std::lock_guard<std::mutex> lck(_mapMtx);
        return _nameMap.size();
    }

    MWorkerListItem::Ptr getWorkerNamed(uint32_t name) {
        std::lock_guard<std::mutex> lck(_mapMtx);
        auto iter = _nameMap.find(name);
        if (iter == _nameMap.end()) { return nullptr; }
        return iter->second;
    }

    std::pair<std::vector<MWorkerListItem::Ptr>, std::vector<MWorkerListItem::Ptr>>
        getActiveInactiveWorkerLists();

    std::string dump() const;

protected:
    void _flagListChange();

    CentralMaster* _central;
    std::map<uint32_t, MWorkerListItem::Ptr> _nameMap;
    std::map<NetworkAddress, MWorkerListItem::Ptr> _ipMap;
    bool _wListChanged{false}; ///< true if the list has changed
    BufferUdp::Ptr _stateListData; ///< message
    uint32_t _totalNumberOfWorkers{0}; ///< total number of workers according to the master.
    mutable std::mutex _mapMtx; ///< protects _nameMap, _ipMap, _wListChanged

    std::atomic<uint32_t> _sequenceName{1}; ///< Source of names for workers. 0 is invalid name.

};


}}} // namespace lsst::qserv::loader

#endif // LSST_QSERV_LOADER_MWORKERLIST_H_
