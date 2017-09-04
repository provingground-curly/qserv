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

#include "replica_core/FileServerConnection.h"

// System headers

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <cerrno>
#include <cstring>
#include <stdexcept>

// Qserv headers

#include "lsst/log/Log.h"
#include "replica_core/Configuration.h"
#include "replica_core/ServiceProvider.h"

namespace fs    = boost::filesystem;
namespace proto = lsst::qserv::proto;

namespace {

LOG_LOGGER _log = LOG_GET("lsst.qserv.replica_core.FileServerConnection");

/// The limit of 16 MB fo rthe maximum record size for file I/O and
/// network operations.
const size_t maxFileBufSizeBytes = 16 * 1024 * 1024;

} /// namespace

namespace {
    
typedef std::shared_ptr<lsst::qserv::replica_core::ProtocolBuffer> ProtocolBufferPtr;

/// The context for diagnostic & debug printouts
const std::string context = "FILE-SERVER-CONNECTION  ";

bool isErrorCode (boost::system::error_code  ec,
                  const std::string         &scope) {

    if (ec) {
        if (ec == boost::asio::error::eof)
            LOGS(_log, LOG_LVL_DEBUG, context << scope << "  ** closed **");
        else
            LOGS(_log, LOG_LVL_ERROR, context << scope << "  ** failed: " << ec << " **");
        return true;
    }
    return false;
}

bool readIntoBuffer (boost::asio::ip::tcp::socket &socket,
                     const ProtocolBufferPtr      &ptr,
                     size_t                        bytes) {

    ptr->resize(bytes);     // make sure the buffer has enough space to accomodate
                            // the data of the message.

    boost::system::error_code ec;
    boost::asio::read (
        socket,
        boost::asio::buffer (
            ptr->data(),
            bytes
        ),
        boost::asio::transfer_at_least(bytes),
        ec
    );
    return ! ::isErrorCode (ec, "readIntoBuffer");
}

template <class T>
bool readMessage (boost::asio::ip::tcp::socket  &socket,
                  const ProtocolBufferPtr       &ptr,
                  size_t                         bytes,
                  T                             &message) {
    
    if (!readIntoBuffer (socket,
                         ptr,
                         bytes)) return false;

    // Parse the response to see what should be done next.

    ptr->parse(message, bytes);
    return true;
}

#if 0
bool readLength (boost::asio::ip::tcp::socket  &socket,
                 const ProtocolBufferPtr       &ptr,
                 uint32_t                      &bytes) {

    if (!readIntoBuffer (socket,
                         ptr,
                         sizeof(uint32_t))) return false;
    
    bytes = ptr->parseLength();
    return true;
}
#endif

}   // namespace

namespace lsst {
namespace qserv {
namespace replica_core {

FileServerConnection::pointer
FileServerConnection::create (ServiceProvider         &serviceProvider,
                              const std::string       &workerName,
                              boost::asio::io_service &io_service) {

    return FileServerConnection::pointer (
        new FileServerConnection (
            serviceProvider,
            workerName,
            io_service));
}

FileServerConnection::FileServerConnection (ServiceProvider         &serviceProvider,
                                            const std::string       &workerName,
                                            boost::asio::io_service &io_service)

    :   _serviceProvider {serviceProvider},
        _workerName      {workerName},
        _workerInfo      {serviceProvider.config().workerInfo(workerName)},
        _socket          {io_service},

        _bufferPtr (
            std::make_shared<ProtocolBuffer>(
                serviceProvider.config().requestBufferSizeBytes())),
        _fileName   (),
        _filePtr    (0),
        _fileBufSize(serviceProvider.config().workerFsBufferSizeBytes()),
        _fileBuf    (0) {

    if (!_fileBufSize || _fileBufSize > maxFileBufSizeBytes)
        throw std::invalid_argument("FileServerConnection: the buffer size must be in a range of: 0-" +
                                    std::to_string(maxFileBufSizeBytes) + " bytes. Check the configuration.");

    _fileBuf = new uint8_t[_fileBufSize];
    if (!_fileBuf)
        throw std::runtime_error("FileServerConnection: failed to allocate the buffer, size: " +
                                 std::to_string(maxFileBufSizeBytes) + " bytes.");
}

FileServerConnection::~FileServerConnection () {
    delete [] _fileBuf;
}

void
FileServerConnection::beginProtocol () {
    receiveRequest();
}

void
FileServerConnection::receiveRequest () {

    LOGS(_log, LOG_LVL_DEBUG, context << "receiveRequest");

    // Start with receiving the fixed length frame carrying
    // the size (in bytes) the length of the subsequent message.
    //
    // The message itself will be read from the handler using
    // the synchronous read method. This is based on an assumption
    // that a client sends the whole message (its frame and
    // the message itsef) at once.

    const size_t bytes = sizeof(uint32_t);

    _bufferPtr->resize(bytes);

    boost::asio::async_read (
        _socket,
        boost::asio::buffer (
            _bufferPtr->data(),
            bytes
        ),
        boost::asio::transfer_at_least(bytes),
        boost::bind (
            &FileServerConnection::requestReceived,
            shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred
        )
    );
}

void
FileServerConnection::requestReceived (const boost::system::error_code &ec,
                                       size_t                           bytes_transferred) {

    LOGS(_log, LOG_LVL_DEBUG, context << "requestReceived");

    if ( ::isErrorCode (ec, "requestReceived")) return;

    // Now read the body of the request

    proto::ReplicationFileRequest request;
    if (!::readMessage (_socket, _bufferPtr, _bufferPtr->parseLength(), request)) return;
   
    LOGS(_log, LOG_LVL_INFO, context << "requestReceived  database: " << request.database()
         << ", file: " << request.file());
    
    // Find a file requested by a client
    
    bool available = false; 
    uint64_t size = 0;
    do {
        if (!_serviceProvider.config().isKnownDatabase(request.database())) {
            LOGS(_log, LOG_LVL_ERROR, context << "requestReceived  unknown database: " << request.database());
            break;
        }
        
        boost::system::error_code ec;

        const fs::path        file = fs::path(_workerInfo.dataDir) / request.database() / request.file();
        const fs::file_status stat = fs::status(file, ec);
        if (stat.type() == fs::status_error) {
            LOGS(_log, LOG_LVL_ERROR, context << "requestReceived  failed to check the status of file: " << file);
            break;
        }
        if (!fs::exists(stat)) {
            LOGS(_log, LOG_LVL_ERROR, context << "requestReceived  file does not exist: " << file);
            break;
        }

        size = fs::file_size(file, ec);
        if (ec) {
            LOGS(_log, LOG_LVL_ERROR, context << "requestReceived  failed to get the file size of: " << file);
            break;
        }

        // Open the file and leave its descriptor open

        _fileName = file.string();
        _filePtr  = std::fopen (file.string().c_str(), "rb");
        if (!_filePtr) {
            LOGS(_log, LOG_LVL_ERROR, context << "requestReceived  file open error: " << std::strerror(errno) << ", file: " << file);
            break;
        }
        available = true;
        
    } while (false);

    // Serialize the response into the buffer and send it back to a caller

    proto::ReplicationFileResponse response;
    response.set_available (available);
    response.set_size      (size);

    _bufferPtr->resize();
    _bufferPtr->serialize(response);

    sendResponse ();
}

void
FileServerConnection::sendResponse () {

    LOGS(_log, LOG_LVL_DEBUG, context << "sendResponse");

    boost::asio::async_write (
        _socket,
        boost::asio::buffer (
            _bufferPtr->data(),
            _bufferPtr->size()
        ),
        boost::bind (
            &FileServerConnection::responseSent,
            shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred
        )
    );
}

void
FileServerConnection::responseSent (const boost::system::error_code &ec,
                                    size_t                           bytes_transferred) {

    LOGS(_log, LOG_LVL_DEBUG, context << "responseSent");

    if (::isErrorCode (ec, "sent")) return;
    
    // If the file pointer is not set it means there was a problem with
    // locating/accessing/opening the file. Hence we just finish the protocol
    // right here.

    if (!_filePtr) return;
    
    // The file is open. Begin streaming its content.
    sendData ();
}

void
FileServerConnection::sendData () {

    LOGS(_log, LOG_LVL_DEBUG, context << "sendData  file: " << _fileName);
    
    // Read next record if possible (a failure or EOF)

    const size_t bytes =
        std::fread (_fileBuf,
                    sizeof(uint8_t),
                    _fileBufSize,
                    _filePtr);
    if (!bytes) {
        if (std::ferror(_filePtr))
            LOGS(_log, LOG_LVL_ERROR, context << "sendData  file read error: " << std::strerror(errno) << ", file: " << _fileName);
        else if (std::feof(_filePtr))
            LOGS(_log, LOG_LVL_INFO, context << "sendData  end of file: " << _fileName);
        else
            ;   // This file was empty, or the previous read was aligned exactly on
                // the end of the file.

        std::fclose(_filePtr);
        return;
    }

    // Send the record

    boost::asio::async_write (
        _socket,
        boost::asio::buffer (
            _fileBuf,
            bytes
        ),
        boost::bind (
            &FileServerConnection::dataSent,
            shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred
        )
    );
}

void
FileServerConnection::dataSent (const boost::system::error_code &ec,
                                size_t                           bytes_transferred) {

    LOGS(_log, LOG_LVL_DEBUG, context << "dataSent");

    if (::isErrorCode (ec, "dataSent")) return;

    sendData();
}

}}} // namespace lsst::qserv::replica_core
