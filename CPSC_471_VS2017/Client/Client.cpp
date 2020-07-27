#include "pch.h"
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif

#include <stdio.h>
#include <iostream>
#include <fstream>
#ifdef WIN32
#include <filesystem>
#else
#include <experimental/filesystem>
#endif
#include <string>
#include "Connection.h"

using namespace std;
using namespace connection;
namespace fs = std::experimental::filesystem;

// temp: change to some reasonable value
constexpr long RESPONSE_TIMEOUT_MS = 500000; // how long to wait on a response from the server
constexpr long CONNECTION_WAIT_TIMEOUT = 500000; // how long to wait for server to connect to our data channel

void run_client(Connection::Ptr connection);
void handle_ls(Connection::Ptr connection);
void handle_get(Connection::Ptr connection, const string& filename, const string& outputFname);
void handle_put(Connection::Ptr connection, const string& filename);
void handle_quit(Connection::Ptr connection);

int main()
{
    Connection::initialize();

    try {
        //
        // TODO (client):
        //
        // 1) use command line arguments to connect to server
        // 2) read commands from cin and run appropriate functions
        // 3) implement PUT command (needs implementation on server side also)

        auto connection = Connection::connect("127.0.0.1", 25000); // localhost
        //auto connection = Connection::connect("192.168.5.130", 25000); // virtualbox linux (set to appropriate IP)
        cout << "Successfully connected to server! using port " << connection->host_port() << endl;

        run_client(connection);
    }
    catch (const ConnectionException& e) {
        cerr << "error connecting to server: " << e.what() << endl;
    }

    Connection::deinitialize();

    return 0;
}


void run_client(Connection::Ptr connection) {
    Message msg;
    bool timeout = false;

    ZERO_MSG(&msg);

    msg.msgid = MSGID::MESSAGE_HELLO;
    connection->send(msg);

    // expect greetings back
    auto bytesReceived = connection->receive(&msg, 20000, &timeout);

    if (timeout) {
        cout << connection->identify_remote() << " timed out; closing connection" << endl;
        return;
    }

    if (msg.msgid != MSGID::MESSAGE_HELLO || bytesReceived < MESSAGE_BYTE_LEN) {
        cout << connection->identify_remote() << " sent incorrect response; closing connection" << endl;
        return;
    }

    cout << "Established connection with " << msg.payload << endl;

    // test out commands
    handle_ls(connection);
    handle_get(connection, "testfile_small.txt", "retrieved.txt");
	handle_put(connection, "testfile_small1.txt");
    handle_quit(connection);

    // TODO: read commands from user instead
}


void handle_ls(Connection::Ptr connection) {
    auto msg = MAKE_MSG(MSGID::MESSAGE_LS);
    Message response; ZERO_MSG(&response);
    bool bListen = true;
    bool timedOut = false;

    Connection::StopListeningQuery stopListening = [&bListen]() { return !bListen; };
    Connection::ErrorCallback onError = [](const ConnectionException& ce) { throw ce; return false; };
    Connection::SocketCreatedCallback onListen = [&connection, &msg](const std::string& remoteHost, port_t port) {
        // let the server know which port to connect to
        msg.port = port;
        connection->send(msg);
    };

    Connection::ConnectionEstablishedCallback onEstablished = [&](Connection::Ptr dataChannel) {
        // expecting OK or ERROR from server
        connection->receive(&response, RESPONSE_TIMEOUT_MS, &timedOut);

        if (timedOut)
            throw ConnectionException("server response timed out");

        switch (response.msgid) {
            case MSGID::MESSAGE_ERROR:
                bListen = false;
                cerr << "error: " << response.to_string() << endl;
                return;

            case MSGID::MESSAGE_OK:
                // all good to go
                break;

        default:
            throw ConnectionException(("unexpected server response: " + response.to_string()));
        }

        std::stringstream buf;
        NetworkDataStream ds(buf);

        auto bytesLeft = response.datalen;

        while (bytesLeft > 0) {
            // return value of received is guaranteed to be int or smaller
            auto received = static_cast<uint64_t>(dataChannel->receive(ds, bytesLeft > CHUNK_SIZE ? CHUNK_SIZE : static_cast<int>(bytesLeft)));
            bytesLeft -= received;
        }

        cout << "Bytes received: " << response.datalen << endl;
        cout << "List of files on server:\n" << buf.str() << endl;
        dataChannel->shutdown();
    };

    Connection::welcome(Connection::PORT_ANY, stopListening, onListen, onEstablished, onError, true, CONNECTION_WAIT_TIMEOUT);
}


// note: refactor to remove common connection code
void handle_get(Connection::Ptr connection, const string& filename, const string& outputFname) {
    auto command = MAKE_MSG(MSGID::MESSAGE_GET, filename);
    Message response; ZERO_MSG(&response);
    bool bListen = true;
    bool timedOut = false;
    fstream output;

    // try to open file before doing anything; this might fail on our end and we
    // needn't send anything to the server at all
    output.open(outputFname.c_str(), output.binary | output.in | output.out | output.trunc);

    // TODO: handle case where file already exists locally, doesn't exist on server, and server responds
    // with an error message, effectively deleting local file

    if (!output.good()) {
        cerr << "Couldn't open " << outputFname << " for writing" << endl;
        return;
    }

    Connection::StopListeningQuery stopListening = [&bListen]() { return !bListen; };
    Connection::ErrorCallback onError = [](const ConnectionException& ce) {
        throw ce; return false;
    };
    Connection::SocketCreatedCallback onListen = [&command, &connection](const std::string& remoteHost, port_t port) {
        command.port = port;
        connection->send(command);
    };


    Connection::ConnectionEstablishedCallback onEstablished = [&](Connection::Ptr dataChannel) {
        // expecting OK or ERROR from server
        connection->receive(&response, RESPONSE_TIMEOUT_MS, &timedOut);

        if (timedOut)
            throw ConnectionException("server response timed out");

        switch (response.msgid) {
            case MSGID::MESSAGE_ERROR:
                bListen = false;
                cerr << "error: " << response.to_string() << endl;
                return;

            case MSGID::MESSAGE_OK:
                // all good to go
                break;

            default:
                throw ConnectionException(("unexpected server response: " + response.to_string()));
        }


        NetworkDataStream ds(output);

        auto bytesLeft = response.datalen;

        try {
            while (bytesLeft > 0) {
                // return value of received is guaranteed to be int or smaller
                auto received = static_cast<uint64_t>(dataChannel->receive(ds, bytesLeft > CHUNK_SIZE ? CHUNK_SIZE : static_cast<int>(bytesLeft)));
                bytesLeft -= received;

                output.flush();
            }
        }
        catch (const ConnectionException& ce) {
            cerr << "Failed to receive " << outputFname << ": missing " << bytesLeft << " bytes" << endl;
            cerr << "Error: " << ce.what() << endl;
        }

        const auto bytesReceived = response.datalen - bytesLeft;

        // confirm we got what we expected
        output.flush();
        dataChannel->shutdown();

        if (bytesLeft == 0) {
            cout << "Received " << outputFname << " successfully!\n\ttransferred " << bytesReceived << " bytes" << endl;
        } else cerr << "Failed to receive " << outputFname << ": received " << bytesReceived << " of " << response.datalen << " bytes" << endl;
    };


    Connection::welcome(Connection::PORT_ANY, stopListening, onListen, onEstablished, onError, true);
}


void handle_put(Connection::Ptr connection, const string& filename) {
	auto command = MAKE_MSG(MSGID::MESSAGE_PUT, filename);
	Message response; ZERO_MSG(&response);
	fstream input;
	Message putMessage;
	bool bListen = true;
	bool timedOut = false;

	input.open(filename.c_str(), input.binary | input.in | input.out | input.trunc);

	if (!input.good()) {
		cerr << "Couldn't open " << filename << " for writing" << endl;
		return;
	}

	Connection::StopListeningQuery stopListening = [&bListen]() { return !bListen; };
	Connection::ErrorCallback onError = [](const ConnectionException& ce) {
		throw ce; return false;
	};
	Connection::SocketCreatedCallback onListen = [&command, &connection](const std::string& remoteHost, port_t port) {
		command.port = port;
		connection->send(command);
	};

	Connection::ConnectionEstablishedCallback onEstablished = [&](Connection::Ptr dataChannel) {
		// expecting OK or ERROR from server
		connection->receive(&response, RESPONSE_TIMEOUT_MS, &timedOut);

		if (timedOut)
			throw ConnectionException("server response timed out");

		switch (response.msgid) {
		case MSGID::MESSAGE_ERROR:
			bListen = false;
			cerr << "error: " << response.to_string() << endl;
			return;

		case MSGID::MESSAGE_OK:
			// all good to go
			break;

		default:
			throw ConnectionException(("unexpected server response: " + response.to_string()));
		}

		NetworkDataStream ds(input);

		auto bytesLeft = response.datalen;

		try {
			while (bytesLeft > 0) {
				// return value of sent is guaranteed to be int or smaller
				auto received = static_cast<uint64_t>(dataChannel->receive(ds, bytesLeft > CHUNK_SIZE ? CHUNK_SIZE : static_cast<int>(bytesLeft)));
				bytesLeft -= received;

				input.flush();
			}
		}
		catch (const ConnectionException& ce) {
			cerr << "Failed to send " << filename << ": missing " << bytesLeft << " bytes" << endl;
			cerr << "Error: " << ce.what() << endl;
		}
	};

	Connection::welcome(Connection::PORT_ANY, stopListening, onListen, onEstablished, onError, true);
}


void handle_quit(Connection::Ptr connection) {
    // let server know we're done
    const auto msg = MAKE_MSG(MSGID::MESSAGE_QUIT, "Goodbye!");
    connection->send(msg);
    connection->shutdown();
}
