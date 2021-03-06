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
#ifndef LSST_QSERV_REPLICA_TESTECHOQSERVMGTREQUEST_H
#define LSST_QSERV_REPLICA_TESTECHOQSERVMGTREQUEST_H

// System headers
#include <memory>
#include <string>
#include <vector>

// Third party headers

// Qserv headers
#include "replica/QservMgtRequest.h"
#include "replica/ServiceProvider.h"
#include "wpublish/TestEchoQservRequest.h"

// This header declarations
namespace lsst {
namespace qserv {
namespace replica {

/**
  * Class TestEchoQservMgtRequest implements a special kind of requests
  * for testing Qserv workers.
  */
class TestEchoQservMgtRequest : public QservMgtRequest {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<TestEchoQservMgtRequest> Ptr;

    /// The function type for notifications on the completion of the request
    typedef std::function<void(Ptr)> CallbackType;

    // Default construction and copy semantics are prohibited

    TestEchoQservMgtRequest() = delete;
    TestEchoQservMgtRequest(TestEchoQservMgtRequest const&) = delete;
    TestEchoQservMgtRequest& operator=(TestEchoQservMgtRequest const&) = delete;

    ~TestEchoQservMgtRequest() final = default;

    /**
     * Static factory method is needed to prevent issues with the lifespan
     * and memory management of instances created otherwise (as values or via
     * low-level pointers).
     *
     * @param serviceProvider
     *   reference to a provider of services for accessing Configuration,
     *   saving the request's persistent state to the database
     *
     * @param worker
     *   the name of a worker to send the request to
     *
     * @param data
     *   the data string to be echoed back by the worker (if successful)
     *
     * @param onFinish
     *   (optional) callback function to be called upon request completion
     *
     * @return
     *   pointer to the created object
     */
    static Ptr create(ServiceProvider::Ptr const& serviceProvider,
                      std::string const& worker,
                      std::string const& data,
                      CallbackType const& onFinish=nullptr);

    /// @return input data string sent to the worker
    std::string const& data() const { return _data; }

    /**
     * @return
     *   data string echoed back by the worker
     *
     * @note
     *   the method will throw exception std::logic_error if called
     *   before the request finishes or if it's finished with any
     *   status but SUCCESS.
     */
    std::string const& dataEcho() const;

    /// @see QservMgtRequest::extendedPersistentState()
    std::list<std::pair<std::string,std::string>> extendedPersistentState() const override;

protected:

    /// @see QservMgtRequest::startImpl
    void startImpl(util::Lock const& lock) final;

    /// @see QservMgtRequest::finishImpl
    void finishImpl(util::Lock const& lock) final;

    /// @see QservMgtRequest::notify
    void notify(util::Lock const& lock) final;

private:

    /// @see TestEchoQservMgtRequest::created()
    TestEchoQservMgtRequest(ServiceProvider::Ptr const& serviceProvider,
                            std::string const& worker,
                            std::string const& data,
                            CallbackType const& onFinish);

    /**
     * Carry over results of the request into a local storage.
     * 
     * @param lock
     *   lock on QservMgtRequest::_mtx must be acquired by a caller of the method
     *
     * @param data
     *   data string returned by a worker
     */
    void _setData(util::Lock const& lock,
                  std::string const& data);


    // Input parameters

    std::string const _data;
    CallbackType      _onFinish;    /// @note this object is reset after finishing the request

    /// A request to the remote services
    wpublish::TestEchoQservRequest::Ptr _qservRequest;

    /// The data string returned by the Qservr worker
    std::string _dataEcho;
};

}}} // namespace lsst::qserv::replica

#endif // LSST_QSERV_REPLICA_TESTECHOQSERVMGTREQUEST_H
