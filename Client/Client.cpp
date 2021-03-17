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
#include <limits>
#include <map>
#include <cctype>
#include "Connection.h"
#include "SyncStream.h"

using namespace std;
using namespace connection;
namespace fs = std::experimental::filesystem;


constexpr long RESPONSE_TIMEOUT_MS = 10000; // how long to wait on a response from the server
constexpr long CONNECTION_WAIT_TIMEOUT = 10000; // how long to wait for server to connect to our data channel
constexpr int kTerminalLength = 79;


// function prototypes
bool parse_arguments(int argc, const char** argv, string& address, port_t& port);

void run_client();
bool parse_command(MSGID command);
bool exchange_greetings();
void handle_ls();
void handle_get(const string& filename);
void handle_put(const string& filename);
void handle_quit();
void make_header(const std::string& msg, uint64_t bytes);
bool get_yesno();

struct CommandLookupComp {
    bool operator()(const string& lhs, const string& rhs) const {
        std::string ulhs;
        std::string urhs;

        std::transform(lhs.cbegin(), lhs.cend(), std::back_inserter(ulhs), [](char c) { return static_cast<char>(std::toupper(c)); });
        std::transform(rhs.cbegin(), rhs.cend(), std::back_inserter(urhs), [](char c) { return static_cast<char>(std::toupper(c)); });

        return ulhs.compare(urhs) < 0;
    }
};

typedef map<string, MSGID, CommandLookupComp> CommandLookupTable;
const CommandLookupTable kCommands =
{
    { "GET",    MSGID::MESSAGE_GET  },
    { "PUT",    MSGID::MESSAGE_PUT  },
    { "LS",     MSGID::MESSAGE_LS   },
    { "QUIT",   MSGID::MESSAGE_QUIT },
    { "Q",      MSGID::MESSAGE_QUIT },
    { "EXIT",   MSGID::MESSAGE_QUIT }
};

Connection::Ptr kControl;

int main(int argc, const char** argv)
{
    string address;
    port_t port;

    if (!parse_arguments(argc, argv, address, port))
        return EXIT_FAILURE;

    Connection::initialize();

    try {
        kControl = Connection::connect(address, port);
    }
    catch (const ConnectionException& e) {
        cerr << "error connecting to server: " << e.what() << endl;
    }

    try {
        run_client();
    }
    catch (const std::exception& e) {
        cerr << "error: " << e.what() << "\nConnection closed." << endl;
    }

    Connection::deinitialize();

    return 0;
}


void run_client() {
    if (!exchange_greetings()) return;

    // read commands from user instead
    string input;
    CommandLookupTable::const_iterator it;

    do {
        cout << "ftp> ";
        cin >> input;

        if (input.empty()) continue;
        
        // try to match input with command
        it = kCommands.find(input);

        if (it == kCommands.cend()) {
            cerr << "invalid command. Valid commands are:\n";

            for (const auto& kvp : kCommands)
                cerr << "\t" << kvp.first << endl;
            cerr << endl;
            continue;
        }

    } while (it == kCommands.cend() /* no command chosen */ || parse_command(it->second));

    cout << "Goodbye!" << endl;
}


// return true if should continue running
// avoids having a bunch of complexity in run_client: each command might need arguments user supplied
bool parse_command(MSGID command) {
    switch (command) {
        case MSGID::MESSAGE_LS:
            handle_ls();
            break;

        case MSGID::MESSAGE_GET:
            // should be another argument: filename
            {
                string fname;
                cin >> fname;

                handle_get(fname);
            }
            break;


        case MSGID::MESSAGE_PUT:
        
            cerr << "not implemented yet" << endl;
            break;

        case MSGID::MESSAGE_QUIT:
            handle_quit();
            return false;

        default:
            cerr << "Unrecognized command: " << command << endl;
    }

    return true;
}



bool exchange_greetings() {
    Message msg;
    bool timeout = false;

    ZERO_MSG(&msg);

    msg.msgid = MSGID::MESSAGE_HELLO;
    kControl->send(msg);

    // expect greetings back
    auto bytesReceived = kControl->receive(&msg, 20000, &timeout);

    if (timeout) {
        cout << kControl->identify_remote() << " timed out; closing connection" << endl;
        return false;
    }

    if (msg.msgid != MSGID::MESSAGE_HELLO || bytesReceived < MESSAGE_BYTE_LEN) {
        cout << kControl->identify_remote() << " sent incorrect response; closing connection" << endl;
        return false;
    }

    cout << "Established connection with " << msg.payload << endl;
    return true;
}


void handle_ls() {
    auto msg = MAKE_MSG(MSGID::MESSAGE_LS);
    Message response; ZERO_MSG(&response);
    bool bListen = true;
    bool timedOut = false;

    Connection::StopListeningQuery stopListening = [&bListen]() { return !bListen; };
    Connection::ErrorCallback onError = [](const ConnectionException& ce) { throw ce; return false; };
    Connection::SocketCreatedCallback onListen = [&msg](const std::string& remoteHost, port_t port) {
        // let the server know which port to connect to
        msg.port = port;
        kControl->send(msg);
    };

    Connection::ConnectionEstablishedCallback onEstablished = [&](Connection::Ptr dataChannel) {
        // expecting OK or ERROR from server
        kControl->receive(&response, RESPONSE_TIMEOUT_MS, &timedOut);

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

        make_header("Listing files on server", response.datalen);
        cout << buf.str() << endl << endl;

        dataChannel->shutdown();
    };

    Connection::welcome(Connection::PORT_ANY, stopListening, onListen, onEstablished, onError, true, CONNECTION_WAIT_TIMEOUT);
}


// note: refactor to remove common connection code
void handle_get(const string& filename) {
    auto command = MAKE_MSG(MSGID::MESSAGE_GET, filename);
    Message response; ZERO_MSG(&response);
    bool bListen = true;
    bool timedOut = false;
    fstream output;

    if (fs::exists(filename)) {
        cout << "WARNING: " << filename << " already exists. Overwrite? Y/N" << endl;

        if (!get_yesno()) {
            cout << "Command cancelled" << endl;
            return;
        }
    }


    Connection::StopListeningQuery stopListening = [&bListen]() { 
        return !bListen; 
    };

    Connection::ErrorCallback onError = [](const ConnectionException& ce) {
        throw ce; return false;
    };

    Connection::SocketCreatedCallback onListen = [&](const std::string& remoteHost, port_t port) {
        command.port = port;
        kControl->send(command);

        // expecting OK or ERROR from server
        kControl->receive(&response, RESPONSE_TIMEOUT_MS, &timedOut);

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
    };


    Connection::ConnectionEstablishedCallback onEstablished = [&](Connection::Ptr dataChannel) {
        output.open(filename.c_str(), output.binary | output.in | output.out | output.trunc);

        if (!output.good()) {
            cerr << "Couldn't open " << filename << " for writing" << endl;
            return;
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
            cerr << "Failed to receive " << filename << ": missing " << bytesLeft << " bytes" << endl;
            cerr << "Error: " << ce.what() << endl;
        }

        const auto bytesReceived = response.datalen - bytesLeft;

        // confirm we got what we expected
        output.flush();
        dataChannel->shutdown();

        if (bytesLeft == 0) {
            cout << "Received " << filename << " successfully!\n\tTransferred " << bytesReceived << " bytes" << endl;
        } else cerr << "Failed to receive " << filename << ": received " << bytesReceived << " of " << response.datalen << " bytes" << endl;
    };


    Connection::welcome(Connection::PORT_ANY, stopListening, onListen, onEstablished, onError, true);
}


void handle_put(const string& filename) {
	Connection::Ptr dataChannel;
	auto command = MAKE_MSG(MSGID::MESSAGE_PUT, filename);
	Message response; ZERO_MSG(&response);
	fstream input;
	fs::path filePath(filename);
	std::streampos fileSize = 0;
	bool bListen = true;
	bool timedOut = false;

	// first check for failure scenarios:
	// did they actually specify a filename?
	if (filename.empty()) {
		response = MAKE_EMSG(MSGECODE::ERR_INVALID_FILENAME);
        cerr << "error: filename required" << endl;
        return;
	}

	// did they try to use some kind of path (not allowed for now)
	if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
		cerr << "'" << filename << "' contains a path which is not permitted";
		return;
	}

	// does their filename exist?
	if (!fs::exists(fs::path(filename))) {
		cerr << "File '" << filename << "' not found";
		return;
	}

	// did they specify something that isn't a file?
	if (!fs::is_regular_file(filePath)) {
		cerr << "'" << filename << "' is not a file - only files may be sent with this command";
		return;
	}


    input.open(filename.c_str(), input.binary | input.in | input.out );

	if (!input.good() || !input.is_open()) {
        cerr << "Failed to open '" << filename << "'" << endl;
        return;
    }

	// find out number of bytes in the file
    input.seekg(0, input.end);
	fileSize = input.tellg();
    input.seekg(0, input.beg);

    command.datalen = fileSize;

	// it's possible seek failed for some reason, and if we can't tell the size
	// of the file we're toast already
	if (!input.good()) {
		cerr << "Unable to determine length of '" << filename << "'" << endl;
        return;
	}
	

    // file is open, all checks on client-side have passed: prepare to send the file

	Connection::StopListeningQuery stopListening = [&bListen]() { 
        return !bListen; 
    };

	Connection::ErrorCallback onError = [](const ConnectionException& ce) {
		throw ce; return false;
	};

	Connection::SocketCreatedCallback onListen = [&](const std::string& remoteHost, port_t port) {
        // once port is listening, we know which port server should connect to
		command.port = port;
        kControl->send(command);

        // expecting OK or ERROR from server
        // ERROR would tell us server isn't going to be connecting at all
        kControl->receive(&response, RESPONSE_TIMEOUT_MS, &timedOut);

        if (timedOut)
            throw ConnectionException("server response timed out");

        switch (response.msgid) {
            case MSGID::MESSAGE_ERROR:
                bListen = false;
                cerr << response.to_string() << endl;
                return;

            case MSGID::MESSAGE_OK:
                // all good to go
                break;

            default:
                throw ConnectionException(("unexpected server response: " + response.to_string()));
        }
	};


	Connection::ConnectionEstablishedCallback onEstablished = [&](Connection::Ptr dataChannel) {
		// now actually stream the data over to the server. Connection handles splitting
		// this in chunks for us
		NetworkDataStream stream(input);

		const auto bytesSent = dataChannel->send(stream);

		dataChannel->shutdown();

		// that's it, just make sure we sent everything and let the data connection close
		if (bytesSent != fileSize) {
			cout << "Transmission incomplete! Sent " << bytesSent << " bytes of " << fileSize << " bytes to " << dataChannel->identify_remote() << endl;
		}
		else cout << "Successfully sent transferred '" << filename << "' to server: " << bytesSent << " bytes were sent" << endl;
	};

    // callbacks are set up, now kick off actual operation
	Connection::welcome(Connection::PORT_ANY, stopListening, onListen, onEstablished, onError, true, CONNECTION_WAIT_TIMEOUT);
}


void handle_quit() {
    // let server know we're done
    const auto msg = MAKE_MSG(MSGID::MESSAGE_QUIT, "Goodbye!");
    kControl->send(msg);
    kControl->shutdown();
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


#undef max

bool parse_arguments(int argc, const char** argv, string& address, port_t& port) {
    string serverAddress;
    string serverPort;
    auto iPort = std::stoi("0"); // decltype only available in C++17

    if (argc != 3) goto print_usage;

    serverAddress = *(argv + 1);
    serverPort = *(argv + 2);

    // port must be a numerical value obviously
    try {
        iPort = std::stoi(serverPort.c_str());

        // deal with overflow potential (if port isn't a 2-byte type)
        if (sizeof(port_t) < sizeof(iPort)) {
            const auto maxValue = std::numeric_limits<port_t>().max();

            if (iPort > maxValue) {
                cerr << "'" << iPort << "' is out of range. Range is [1, " << maxValue << "]" << endl;
                goto print_usage;
            } 
        }
    } catch (...) {
        cerr << "'" << serverPort << "' is not a numeric value" << endl;
        goto print_usage;
    }

    port = static_cast<port_t>(iPort);
    address = serverAddress;

    return !serverAddress.empty() && port > 0;

print_usage:
    cerr << "Usage: " << getExe(*argv) << " <server machine> <server port>" << endl;
    return false;
}


void make_header(const std::string& msg, uint64_t bytes) {

    cout << string(kTerminalLength, '*') << endl;
    cout << "* " << msg << string(kTerminalLength - 2 - msg.length() - 1, ' ') << "*" << endl;

    std::string secondMsg = string("* Bytes transferred: ") + std::to_string(bytes);
    
    secondMsg += string(kTerminalLength - secondMsg.length() - 1, ' ');
    secondMsg += '*';

    cout << secondMsg << endl;
    cout << string(kTerminalLength, '*') << endl;
}


bool get_yesno() {
    string input;

    do {
        cin >> input;

        if (input.empty()) continue;
        if (std::toupper(input.front()) == 'Y') return true;
        if (std::toupper(input.front()) == 'N') return false;

        cout << "Y/N? ";
    } while (true);
}