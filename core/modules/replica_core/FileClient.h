// -*- LSST-C++ -*-
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
#ifndef LSST_QSERV_REPLICA_CORE_FILECLIENT_H
#define LSST_QSERV_REPLICA_CORE_FILECLIENT_H

/// FileClient.h declares:
///
/// class FileClient
/// (see individual class documentation for more information)

// System headers

#include <boost/asio.hpp>
#include <ctime>
#include <memory>       // auto_ptr, shared_ptr, enable_shared_from_this
#include <stdexcept>
#include <string>

// Qserv headers

// Forward declarations

// This header declarations

namespace lsst {
namespace qserv {
namespace replica_core {

// Forward declarations

class DatabaseInfo;
class ProtocolBuffer;
class ServiceProvider;
class WorkerInfo;

/**
 * The class represents exceptions thrown by FileClient on errors
 */
class FileClientError
    :   public std::runtime_error {
public:
    FileClientError (const std::string& msg)
        :   std::runtime_error(msg) {
    }
};

/**
  * Class FileClient is used for handling incomming connections to
  * the file delivery service. Each instance of this class will be runing
  * in its own thread.
  */
class FileClient
    :   public std::enable_shared_from_this<FileClient>  {

public:

    /// The pointer type for instances of the class
    typedef std::shared_ptr<FileClient> pointer;

    /**
     * Open a file and return a smart pointer to an object of this class.
     * If the operation is successfull then a valid pointer will be returned
     * and the file content could be read via method FileClient::read().
     * Otherwise return the null pointer.
     *
     * @param serviceProvider - for configuration, etc. services
     * @param workerName      - the name of a worker where the file resides
     * @param databaseName    - the name of a atabase the file belongs to
     * @param fileName        - the file to read or examine
     */
    static pointer open (ServiceProvider   &serviceProvider,
                         const std::string &workerName,
                         const std::string &databaseName,
                         const std::string &fileName);
    /**
     * Open a file and return a smart pointer to an object of this class.
     * If the operation is successfull then a valid pointer will be returned.
     * If the operation failes the method will return the null pointer.
     * 
     * ATTENTION:
     *   Unlike the previous method FileClient::open() the returned file object
     *   can't be used to read the file content (via FileClient::read()).
     *   The method of opening files is meant to be used for checking the availability
     *   of files and getting various metadata (size, etc.) about the files.
     *   Any attempts to call method FileClient::read() will result in
     *   throwing FileClientError.
     *
     * @param serviceProvider - for configuration, etc. services
     * @param workerName      - the name of a worker where the file resides
     * @param databaseName    - the name of a atabase the file belongs to
     * @param fileName        - the file to read or examine
     */
    static pointer stat (ServiceProvider   &serviceProvider,
                         const std::string &workerName,
                         const std::string &databaseName,
                         const std::string &fileName);
    
    // Default construction and copy semantics are proxibited

    FileClient () = delete;
    FileClient (FileClient const&) = delete;
    FileClient & operator= (FileClient const&) = delete;

    /// Destructor
    virtual ~FileClient ();

    // Trivial accessors

    const std::string& worker   () const;
    const std::string& database () const;
    const std::string& file     () const;

    /// The size of a file (as reported by a server)
    size_t size () const { return _size; }

    /// The last modification time (mtime) of the file
    std::time_t mtime () const { return _mtime; }

    /**
     * Read (up to, but not exceeding) the specified number of bytes into a buffer.
     *
     * The method will throw the FileClientError exception should any error
     * occured during the operation. Illegal parameters (zero buffer pointer
     * or the buffer size of 0) will be reported by std::invalid_argument exception.
     *
     * @param buf     - a pointer to a valid buffer where the data will be placed
     * @param bufSize - a size of the buffer (would determine the maximum number of bytes
     *                  which can be read at a single call to the method)
     *
     * @return the actual number of bytes read or 0 if the end of file reached
     */
    size_t read (uint8_t* buf, size_t bufSize);

private:

    /**
     * Construct an object with the specified configuration.
     *
     * The constructor may throw the std::invalid_argument exception after
     * validating its arguments.
     * 
     * @param serviceProvider - for configuration, etc. services
     * @param workerName      - the name of a worker where the file resides
     * @param databaseName    - the name of a atabase the file belongs to
     * @param fileName        - the file to read or examine
     * @param readContent     - indicates if a file is open for reading its content   
     */
    FileClient (ServiceProvider   &serviceProvider,
                const std::string &workerName,
                const std::string &databaseName,
                const std::string &fileName,
                bool               readContent);

    /**
     * Try opening the file. Return 'true' if successfull.
     */
    bool openImpl ();

private:

    /// Cached descriptor of the validated database
    const WorkerInfo  &_workerInfo;

    /// Cached parameters of the validated worker
    const DatabaseInfo &_databaseInfo;

    /// The name of a file to be read
    std::string _fileName;

    /// The flag indicating of the file was open with an intend of reading
    // its content
    bool _readContent;

    /// Buffer for data moved over the network
    std::auto_ptr<ProtocolBuffer> _bufferPtr;

    // The mutable state of the object

    boost::asio::io_service      _io_service;
    boost::asio::ip::tcp::socket _socket;

    /// The size of the file in bytes (to be determined after contacting a server)
    size_t _size;

    /// The last modification time (mtime) of the file
    std::time_t _mtime;

    /// The flag which wil be set after hitting the end of the input stream
    bool _eof;
};

}}} // namespace lsst::qserv::replica_core

#endif // LSST_QSERV_REPLICA_CORE_FILECLIENT_H