#include "stdafx.h"
#include <memory>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <chrono>

#ifndef WIN32
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#endif

#include "Connection.h"


constexpr std::stringstream::openmode binary_stream = std::stringstream::in | std::stringstream::out | std::stringstream::binary;
constexpr long SHUTDOWN_TIMEOUT_MS = 60000;


using std::ostringstream;

#ifndef WIN32

constexpr int SOCKET_ERROR = -1;
constexpr int INVALID_SOCKET = -1;
constexpr int SD_SEND = SHUT_WR;

typedef bool BOOL;
constexpr BOOL TRUE = true;
constexpr BOOL FALSE = false;

typedef uint16_t DWORD;
typedef fd_set FD_SET;

typedef struct timeval TIMEVAL;
typedef struct sockaddr SOCKADDR;

#define ZeroMemory(buf, n) memset((buf), 0, (n))
#define ADDR_ANY INADDR_ANY

#else // is WIN32

typedef int socklen_t;
#define TEMP_FAILURE_RETRY(expr) (expr)

#endif


namespace connection {
#ifdef WIN32
    static bool g_winsockInit = false;

    void Connection::initialize() {
        if (!g_winsockInit) {
            WSADATA wsaData;

            WSAStartup(MAKEWORD(2, 2), &wsaData);
            g_winsockInit = true;
        }
    }


    void Connection::deinitialize() {
        if (g_winsockInit) {
            WSACleanup();
            g_winsockInit = false;
        }
    }

    void closesocket(socket_t socket) {
        ::closesocket(socket);
    }

#else // !WIN32

    void Connection::initialize() {}
    void Connection::deinitialize() {}

    void closesocket(socket_t socket) {
        ::close(socket);
    }

    ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
        if (len == 0) return 0;

        return ::recv(sockfd, buf, len, flags);
    }

#endif

    int recv_timeout(socket_t which, char* buf, int len, DWORD flags, long timeoutMs, bool* timedOut = nullptr) {
        FD_SET rfd;
        FD_SET efd;

        FD_ZERO(&rfd);
        FD_ZERO(&efd);

        FD_SET(which, &rfd);
        FD_SET(which, &efd);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = timeoutMs * 1000;

        if (timedOut) *timedOut = false;

        // why not setsockopt? so the caller can determine how long we should wait
        int numReady = TEMP_FAILURE_RETRY(select(FD_SETSIZE, &rfd, nullptr, &efd, timeoutMs > 0 ? &timeout : nullptr));

        if (numReady == SOCKET_ERROR)
            throw ConnectionException::create("exception in select recv_timeout");

        if (numReady == 0) { // timed out?
            if (timedOut) *timedOut = true;
            return 0;
        }


        if (FD_ISSET(which, &rfd) || FD_ISSET(which, &efd)) {
            auto result = recv(which, buf, len, flags);

            if (result == SOCKET_ERROR)
                throw ConnectionException::create("failed to receive data");

            return result;
        }

        // no data to read or errors to catch
        return 0;
    }


    Connection::Connection(socket_t socket, const std::string& hostName, const std::string& remoteName, const port_t& hostPort, const port_t& remotePort)
        : socket_(socket), hostName_(hostName), remoteName_(remoteName), hostPort_(hostPort), remotePort_(remotePort) {
        // empty body
    }


    Connection::~Connection() {
        if (socket_ != INVALID_SOCKET) {
            shutdown();
            closesocket(socket_);
        }
    }


    int Connection::send(NetworkDataStream& data) {
        char buf[CHUNK_SIZE];
        int bytesSent = 0;
        int totalBytesSent = 0;

        while (data.stream_.good()) {
            data.stream_.read(buf, CHUNK_SIZE);
            auto bytesLeft = data.stream_.gcount(); // tells us how many bytes were just read

            if (!data.stream_.good() && bytesLeft == 0) break;

            // it's quite possible the entire chunk won't fit into a single send, so
            // call send multiple times if necessary
            while (bytesLeft > 0) {
                if ((bytesSent = ::send(socket_, buf, static_cast<int>(bytesLeft), 0)) == SOCKET_ERROR)
                    throw ConnectionException::create("error sending data stream (position " + std::to_string(data.stream_.tellg()) + ")");

                bytesLeft -= bytesSent;
                totalBytesSent += bytesSent;
            }
        }

        if (data.stream_.eof()) return totalBytesSent;

        throw std::runtime_error("did not send all data!"); // todo: better exception?
    }


    int Connection::send(const connection::Message& message) {
        std::stringstream buf(binary_stream);
        NetworkDataStream ds(buf);

        if (message.payload.length() > MAX_PAYLOAD_LEN)
            throw std::invalid_argument(("maximum payload length exceeded: " + std::to_string(message.payload.length()) + " > " + (std::to_string(MAX_PAYLOAD_LEN)).c_str()));

        ds << message.msgid << message.length() << message.datalen << message.port;

        ds.write_str(message.payload);

        return send(ds);
    }


    int Connection::receive(NetworkDataStream& data, int howMany, long timeoutMs, bool* timedOut) {
        char buf[CHUNK_SIZE];
        int byteChunkReceived = 0;
        int totalBytes = 0;
        bool internalTimeout = false;

        // even though we're allowing a max of one chunk, there's no guarantee
        // that we can get all that data in a single recv, so continue until we
        // have it all or something breaks

        while (howMany > 0 && data.stream_.good()) {
            if ((byteChunkReceived = recv_timeout(socket_, buf, howMany, 0, timeoutMs, &internalTimeout)) == SOCKET_ERROR)
                throw ConnectionException::create("error receiving data stream (received total of " + std::to_string(totalBytes) + " bytes so far)");

            if (byteChunkReceived > 0) {
                const auto loc = data.stream_.tellp();
                data.stream_.write(buf, byteChunkReceived);
                const auto bytesWritten = data.stream_.tellp() - loc;

                if (bytesWritten != byteChunkReceived)
                    throw std::runtime_error("failed to write bytes to stream");

                howMany -= byteChunkReceived;
                totalBytes += byteChunkReceived;
            } else if (totalBytes == 0) {

                // if the caller has set a selectTimeout, it's not an error to fail to read any bytes
                if (internalTimeout && timeoutMs != TIMEOUT_NEVER)
                    break;

                // if we got 0 bytes and didn't time out, the socket is no longer connected
                if (!internalTimeout)
                    throw ConnectionException::create("client has disconnected unexpectedly", false);

            }
        }

        if (timedOut) *timedOut = internalTimeout;

        if (!data.stream_.good())
            throw std::runtime_error("failed to receive data"); // todo: better exception?

        return totalBytes;
    }


    int Connection::receive(connection::Message* pMsg, long timeoutMs, bool* usertimedOut) {
        int bytesReceived = 0;
        bool timedOut = false;
        std::stringstream buf(binary_stream);
        NetworkDataStream ds(buf);

        ZERO_MSG(pMsg);

        if (usertimedOut) *usertimedOut = false;

        bytesReceived = receive(ds, MESSAGE_BYTE_LEN, timeoutMs, &timedOut);

        if (usertimedOut) *usertimedOut = timedOut;

        if (timeoutMs != TIMEOUT_NEVER && timedOut) return 0; // timing out is not an error

        if (bytesReceived != MESSAGE_BYTE_LEN)
            throw std::runtime_error(("unexpected number of bytes: " + std::to_string(bytesReceived) + " received, expected " + std::to_string(MESSAGE_BYTE_LEN)).c_str());

        ds >> pMsg->msgid;
        ds >> pMsg->msglen;

        // this message will tell us how many bytes are in the message (TOTAL)
        auto moreBytes = pMsg->msglen - bytesReceived;

        // read that many more bytes
        auto moreBytesReceived = receive(ds, moreBytes); // note no timeout: we definitely know there are more bytes to be had in the case of a Message
        auto totalBytes = bytesReceived + moreBytesReceived;

        if (totalBytes != pMsg->msglen) {
            const auto ex = std::runtime_error(("expected to receive " + std::to_string(pMsg->msglen) + " bytes, but received " + std::to_string(totalBytes)).c_str());
            ZERO_MSG(pMsg);
            throw ex;
        }

        ds >> pMsg->datalen;
        ds >> pMsg->port; // note that port and ecode are same field with same size so it doesn't matter which we use

        pMsg->payload = ds.read_str(pMsg->msglen - MESSAGE_BYTE_LEN); // can infer size of payload by examining message length

        return totalBytes;
    }


    void Connection::welcome(
        port_t port,
        StopListeningQuery& stopListeningQuery,
        SocketCreatedCallback& onCreate,
        ConnectionEstablishedCallback& onConnection,
        ErrorCallback& onAcceptFailure,
        bool singleShot,                           
        long timeoutMs) {

        socket_t welcomeSocket, acceptSocket;
        sockaddr_in service;
        FD_SET descriptors;
        TIMEVAL selectTimeout;
        struct sockaddr_in sin;

        char buf[256] = { '\0' };
        std::string hostName, remoteName;
        port_t srcPort, destPort;
        socklen_t len = sizeof(sin);
#ifdef WIN32
        BOOL optTrue = TRUE;
#else
        int optTrue = 1;
#endif


        // create a socket to listen for incoming requests
        welcomeSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (welcomeSocket == INVALID_SOCKET)
            throw ConnectionException::create("failed to create welcome socket");

        // allow the socket to reuse an address. This is mainly to speed up dev, where otherwise
        // connections would need to time out before the socket became available for use
        if (port != PORT_ANY)
            if (setsockopt(welcomeSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&optTrue, sizeof(optTrue)) < 0)
                throw ConnectionException::create("unable to reuse socket address");

        ZeroMemory(&service, sizeof(service));

        // bind IP address and destPort to socket
        // todo: IPv6 support?
        service.sin_family = AF_INET;

        service.sin_port = htons(port);
        service.sin_addr.s_addr = htonl(ADDR_ANY);

        if (bind(welcomeSocket, (SOCKADDR *) &service, sizeof(service)) == SOCKET_ERROR) {
            const auto connerr = ConnectionException::create("welcome socket bind failed");
            closesocket(welcomeSocket);

            throw connerr;
        }

        if (listen(welcomeSocket, 1) == SOCKET_ERROR) {
            const auto connerr = ConnectionException::create("welcome socket listen failed");

            closesocket(welcomeSocket);

            throw connerr;
        }

        // get port # of listening socket
        if (getsockname(welcomeSocket, (struct sockaddr *)&sin, &len) == SOCKET_ERROR) {
            const auto connerr = ConnectionException::create("getsockname failed");

            closesocket(welcomeSocket);

            throw connerr;
        }

        // find address of listening socket
        if (inet_ntop(AF_INET, &sin.sin_addr, buf, sizeof(buf)) == nullptr) {
            const auto connerr = ConnectionException::create("failed to convert address");
            closesocket(welcomeSocket);

            throw connerr;
        }

        hostName = std::string(buf);
        srcPort = ntohs(sin.sin_port);

        // socket is now actively listening. Use callback in case caller wants to do
        // something when this happens
        onCreate(hostName, srcPort);

        // -------------------------------------------------------
        // begin accepting connections
        // -------------------------------------------------------
        selectTimeout.tv_usec = 50 * 1000; // wait this long for select
                                             // mainly this avoids a busy loop, yet allows the thread
                                             // to react quickly if the callback tells us to stop listening
                                             // for connections
        selectTimeout.tv_sec = 0;

        const auto startTime = std::chrono::high_resolution_clock::now();

        while (!stopListeningQuery()) {
            // check for welcome timeout
            const auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
           
            if (timeoutMs != TIMEOUT_NEVER && std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeoutMs)
                break;


            FD_ZERO(&descriptors);
            FD_SET(welcomeSocket, &descriptors);

            // check for incoming connections
            auto readyCount = TEMP_FAILURE_RETRY (select(FD_SETSIZE, &descriptors, nullptr, nullptr, &selectTimeout));

            if (readyCount == SOCKET_ERROR) {
                const auto connerr = ConnectionException::create("select failed");
                closesocket(welcomeSocket);

                throw connerr;
            }

            if (readyCount == 0)
                continue; // select timed out with no change in socket state

            // if file descriptor is readable, accept is guaranteed not to block
            if (!FD_ISSET(welcomeSocket, &descriptors))
                continue; // no connections ready to be accepted

            // accept this connection
            acceptSocket = accept(welcomeSocket, NULL, NULL);

            if (acceptSocket == INVALID_SOCKET) {

                if (!onAcceptFailure(ConnectionException::create("accept failed"))) {
                    closesocket(welcomeSocket);
                    return;
                }

            }

            // new client established: identify the port it's using and IP address
            len = sizeof(sin);
            ZeroMemory(&sin, len);
            ZeroMemory(buf, sizeof(buf));


            if (getpeername(acceptSocket, (struct sockaddr *)&sin, &len) == SOCKET_ERROR) {
                const auto connerr = ConnectionException::create("getpeername failed");

                closesocket(acceptSocket);

                onAcceptFailure(connerr);

            } else {

                if (inet_ntop(AF_INET, &sin.sin_addr, buf, sizeof(buf)) == nullptr) {
                    const auto connerr = ConnectionException::create("failed to convert address from binary to text");
                    closesocket(welcomeSocket);
                    closesocket(acceptSocket);

                    throw connerr;
                }

                remoteName = std::string(buf);
                destPort = ntohs(sin.sin_port);

                onConnection(std::make_unique<Connection>(acceptSocket, hostName, remoteName, srcPort, destPort));

                // should we allow only a single connection to be welcomed?
                if (singleShot)
                    break;
            }

        } // end while loop


        closesocket(welcomeSocket);
    }


    Connection::Ptr Connection::connect(const std::string& remoteName, port_t destPort) {
        int retVal;
        addrinfo hints, *result, *ptr = nullptr;
        socket_t connectSocket = INVALID_SOCKET;
        struct sockaddr_in sin;
        socklen_t len = sizeof(sin);
        char buf[256] = { '\0' };
        std::string hostName;
        port_t srcPort;

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET; // IPv4, TODO: IPv6
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        // Resolve the server address and destPort
        retVal = getaddrinfo(remoteName.c_str(), std::to_string(destPort).c_str(), &hints, &result);

        if (retVal != 0)
            throw ConnectionException::create("getaddrinfo failed");


        // Attempt to connect to the address(es) returned by
        // the call to getaddrinfo
        for (ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
            // todo: try to support ipv6 also?
            if (ptr->ai_family != AF_INET) continue; // only ipv4 addresses for now

            // attempt to create a socket using provided details
            connectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

            if (connectSocket == INVALID_SOCKET) {
                const auto connerr = ConnectionException::create("error at socket()");
                freeaddrinfo(result);
                closesocket(connectSocket);
                throw connerr;
            }

            // attempt to connect to server using the provided address details
            retVal = TEMP_FAILURE_RETRY(::connect(connectSocket, ptr->ai_addr, (int)ptr->ai_addrlen));

            if (retVal == SOCKET_ERROR) {
                closesocket(connectSocket);

                connectSocket = INVALID_SOCKET;
            } else {
                // we have a connection!

                // grab source srcPort and our (host) name
                if (getsockname(connectSocket, (struct sockaddr *)&sin, &len) == SOCKET_ERROR) {
                    const auto connerr = ConnectionException::create("getsockname error");
                    freeaddrinfo(result);
                    closesocket(connectSocket);
                    throw connerr;
                }

                srcPort = ntohs(sin.sin_port);
                inet_ntop(AF_INET, &sin.sin_addr, buf, sizeof(buf));
                hostName = std::string(buf);

                break;
            }
        }


        if (connectSocket == INVALID_SOCKET) {
            freeaddrinfo(result);
            throw ConnectionException::create("unable to connect to server " + hostName + "!", false);
        }


        Ptr pConnection = std::make_unique<Connection>(connectSocket, hostName, remoteName, srcPort, destPort);
        freeaddrinfo(result);

        return pConnection;
    }


    void Connection::shutdown() {
        char buf[1024];
        int bytes = 0;

        if (socket_ == INVALID_SOCKET)
		    return; // socket is already closed, no need to do anything

        ::shutdown(socket_, SD_SEND);

        try {
            while ((bytes = recv_timeout(socket_, buf, sizeof(buf), 0, SHUTDOWN_TIMEOUT_MS)) != SOCKET_ERROR && bytes > 0) {
                // do nothing: we don't care what we receive, we're just waiting for this to return zero or time out
                // to let us know the other side has been closed
            }
        }
        catch (const ConnectionException&) {
            // swallow
        }

        closesocket(socket_);

        socket_ = INVALID_SOCKET;


        if (bytes == SOCKET_ERROR) {
            // graceful shutdown not possible
            throw ConnectionException("could not shutdown gracefully");
        }
    }


    ConnectionException::ConnectionException(const std::string& humanReadable, int errNo, const std::string& errMsg) :
        std::runtime_error(humanReadable.c_str()), errorNum_(errNo), errorMsg_(errMsg) {}


    ConnectionException ConnectionException::create(const std::string& humanReadable, bool getLastError) {
        if (!getLastError)
            return ConnectionException(humanReadable);

        ostringstream stream;

#ifdef WIN32
        auto err = WSAGetLastError();
        char buf[256];

        // convert error code to something human-readable (if possible)
        const auto numBytes = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,                
            err,                 
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),    
            buf,              
            sizeof(buf),     
            NULL);              

        stream << humanReadable << ": " << std::to_string(err);

        if (numBytes > 0)
            stream << " - " << std::string(buf, numBytes);
#else
        const auto errNum = errno;

        if (errNum != 0)
          stream << humanReadable << ": code " << std::to_string(errno) << " - " << strerror(errNum);
#endif
        return ConnectionException(stream.str());
    }

} // end connection namespace
