#include "pch.h"
#include <iostream>
#include <thread>
#include <atomic>
#include "NetworkDataStream.h"
#include "Connection.h"
#include "ProtocolVer.h"
#include "SyncStream.h"
#include "ClientSession.h"
#ifndef WIN32
#include <signal.h>
#include <string.h>
#endif

using namespace std;
using namespace connection;

atomic_bool g_run = true;

void serve_client(Connection::Ptr client);
bool accept_error(const ConnectionException& ce);
void listen_begins(const std::string& hostName, port_t port);
void set_interrupt();

bool continue_listening() {
    return !g_run.load();
}

inline string getExe(const char* str) {
    string s(str);

    auto idx = s.find_last_of('/');

    if (idx == string::npos)
        idx = s.find_last_of('\\');

    if (idx != string::npos)
        return s.substr(idx + 1);
    else return s;
}


int main(int argc, const char** argv) {
    const auto exeName = getExe(*argv);

    if (argc != 2) {
        cerr << exeName << " usage: " << exeName << " <PORT>" << endl;
        return EXIT_FAILURE;
    }

    int port = 0;

    // technically zero is a valid port, although it would be an odd choice
    if ((port = atoi(argv[1])) == 0 && (*argv[1] != '0' || strlen(argv[1]) != 1)) {
        cerr << argv[1] << " is an invalid port number (range 0 - " << std::to_string(~(port_t)0) << ")" << endl;
        return EXIT_FAILURE;
    }

    Connection::initialize();

    // allow server to be closed with ctrl+c
    set_interrupt();

    Connection::StopListeningQuery lquery = continue_listening;
    Connection::SocketCreatedCallback screate = listen_begins;
    Connection::ErrorCallback ecallback = accept_error;
    Connection::ConnectionEstablishedCallback cest = [](Connection::Ptr client) {
        std::thread thr(serve_client, client);
        thr.detach();
    };

    try {
        // blocks until interrupted: every time a client connects, cest
        // callback will be used (on same thread)
        Connection::welcome(port, lquery, screate, cest, ecallback);
    }
    catch (const ConnectionException& e) {
        sync_cerr.print("Unhandled error: ", e.what(), sync_endl);
        Connection::deinitialize();
        return EXIT_FAILURE;
    }

    Connection::deinitialize();
    return EXIT_SUCCESS;
}


void serve_client(Connection::Ptr client) {
    sync_cout.print("Client ", client->identify_remote(), " has connected", sync_endl);

    try {
        ClientSession(client).serve();
    }
    catch (const std::exception& e) {
        sync_cerr.print("Error caused by ", client->identify_remote(), "\n\t", e.what(), sync_endl);
    }

    try {
        sync_cout.print("Closing connection to ", client->identify_remote(), "...", sync_endl);
        client.reset();
    }
    catch (...) {
        // swallow all exceptions: something has broken, so a graceful shutdown is 
        // probably not going to happen
    }
}


bool accept_error(const ConnectionException& ce) {
    sync_cerr.print("A client attempted to connect, but failed: ", ce.what(), sync_endl);
    return true; // continue listening
}


void listen_begins(const std::string& hostName, port_t port) {
    sync_cout.print("Server is now running on port ", port, sync_endl);
}

#ifndef WIN32
void on_signal(int sig) {
    if (sig == SIGINT)
        g_run.store(false);
}
#endif

void set_interrupt() {
#ifdef WIN32
    SetConsoleCtrlHandler([](DWORD dwType) {
        if (dwType == CTRL_C_EVENT) {
            g_run.store(false);
            return TRUE;
        }

        return FALSE;
    }, TRUE);
#else
    signal(SIGINT, on_signal);
#endif
}
