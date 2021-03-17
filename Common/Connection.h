#pragma once
#include <functional>
#include <exception>
#include <string>
#include <iostream>
#include "Message.h"
#include "NetworkDataStream.h"

#ifdef WIN32
#include <WinSock2.h>
typedef SOCKET socket_t;
#else
#include <sys/socket.h>
#include <sys/types.h>
typedef int socket_t;
#endif

namespace connection {


    class Connection {
        socket_t socket_;
        const std::string hostName_;
        const std::string remoteName_;
        const port_t hostPort_;
        const port_t remotePort_;


    public:
        const static port_t PORT_ANY = 0;
        const static long TIMEOUT_NEVER = 0;

        Connection(socket_t socket, const std::string& hostName, const std::string& remoteName, const port_t& hostPort, const port_t& remotePort);
        ~Connection();


        typedef std::shared_ptr<Connection> Ptr;
        typedef std::function<bool(const class ConnectionException&)> ErrorCallback;
        typedef std::function<void(const std::string&, port_t)> SocketCreatedCallback;
        typedef std::function<void(Ptr)> ConnectionEstablishedCallback;
        typedef std::function<bool()> StopListeningQuery;


        port_t host_port() const { return hostPort_; }
        port_t remote_port() const { return remotePort_; }
        std::string host_name() const { return hostName_; }
        std::string remote_name() const { return remoteName_; }

        std::string identify_host() const {
            std::stringstream ss;

            ss << "Host " << host_name() << ": " << host_port();
            return ss.str();
        }

        std::string identify_remote() const {
            std::stringstream ss;

            ss << "RemoteHost " << remote_name() << ":" << remote_port();
            return ss.str();
        }


        // Returns number of bytes sent
        int send(const connection::Message& message);
        int send(NetworkDataStream& data);

        // Returns number of bytes received
        int receive(connection::Message* pMsg, long timeoutMs = TIMEOUT_NEVER, bool* timedOut = nullptr);
        int receive(NetworkDataStream& data, int numBytes, long timeoutMs = TIMEOUT_NEVER, bool* timedOut = nullptr);

        // blocking: initiates a graceful shutdown. Any extra data being sent will be thrown out
        // once shut down, a connection is permanently closed
        void shutdown();

        static void initialize();
        static void deinitialize();

        static void welcome(
            port_t port,                                        // port to listen on, or 0 for ephemeral
            StopListeningQuery& stopListeningQuery,             // if this returns true, stop listening for connections
            SocketCreatedCallback& onCreate,                    // callback sent when a listening socket is created
            ConnectionEstablishedCallback& onConnection,        // callback when a connection is established
            ErrorCallback& onAcceptFailure,                     // called when a failure happens: return true to keep listening or false to stop
            bool singleShot = false,                            // close welcome socket after a single successful connection
            long timeoutMs = TIMEOUT_NEVER);

        static Ptr connect(const std::string& remoteName, port_t port);



        Connection(const Connection& other) = delete;
        Connection& operator=(const Connection& other) = delete;
    };

    class ConnectionException : public std::runtime_error {
        int errorNum_;
        std::string errorMsg_;

        public:
            ConnectionException(const std::string& human, int errNo = 0, const std::string& errMsg = "");

            int err_no() const { return errorNum_; }
            std::string err_msg() const { return errorMsg_; }

            static ConnectionException create(const std::string& humanReadable, bool getLastError = true);
    };
} // end connection namespace
