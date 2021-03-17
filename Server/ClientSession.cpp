#include "pch.h"
#include <iostream>
#include <fstream>
#ifdef WIN32
#include <filesystem>
#else
#include <experimental/filesystem>
#endif
#include "ClientSession.h"
#include "Message.h"
#include "SyncStream.h"

using namespace connection;
using namespace std;
namespace fs = std::experimental::filesystem;

constexpr long TIMEOUT_HELLO_MS = 10000;                // client has this many milliseconds to respond to a greeting message or we close the connection
constexpr long TIMEOUT_CLIENT_COMMAND_RESPONSE = 10000;
constexpr long TIMEOUT_IDLE = 60000;

ClientSession::ClientSession(ConnectionPtr conn) : control_(conn) {}


void ClientSession::serve() {
    if (!greeting())
        return; // client failed to greet us correctly or at all, we're done serving them

    // todo: serve client
    Message msg;
    bool timedOut = false;
    while (true) {
        // guaranteed to get a message back, or else 0 bytes AND timedOut set
        control_->receive(&msg, TIMEOUT_IDLE, &timedOut);

        // check for timeout
        if (timedOut) {
            sync_cerr.print(control_->identify_remote(), " timed out, closing connection", sync_endl);
            return;
        }

        // decode sent message
        switch (msg.msgid) {
            case MSGID::MESSAGE_LS:
                handle_ls(msg);
                break;

            case MSGID::MESSAGE_GET:
                handle_get(msg);
                break;

            case MSGID::MESSAGE_PUT:
                handle_put(msg);
                break;

            case MSGID::MESSAGE_QUIT:
                handle_quit(msg);
                return;

            default:
                sync_cerr.print(control_->identify_remote(), " sent unexpected message: ", msg, "\nclosing connection to", control_->identify_remote(), sync_endl);
                return;
        }
    }
}


// exchange greetings according to protocol
bool ClientSession::greeting() {
    // send hello to client
    auto hello = MAKE_MSG(MSGID::MESSAGE_HELLO, MAKE_VERSION("Server", "welcome"));

    if (control_->send(hello) != hello.length()) {
        sync_cerr.print("failed to send greeting message to ", control_->identify_remote(), sync_endl);
        return false;
    }

    // expecting a hello from client at this point
    Message response;
    bool timeout = false;
    int bytesReceived = control_->receive(&response, TIMEOUT_HELLO_MS, &timeout);

    if (timeout) {
        sync_cerr.print(control_->identify_remote(), " failed to respond to greeting message", sync_endl);
        return false;
    }

    if (response.msgid != MSGID::MESSAGE_HELLO || bytesReceived < MESSAGE_BYTE_LEN) {
        sync_cerr.print(control_->identify_remote(), " sent incorrect greeting response", sync_endl);
        return false;
    }

    return true;
}


void ClientSession::handle_ls(const Message& clientCommand) {
    // client is listening for our contact and won't begin reading data
    // until we send OK message

    try {
        Connection::Ptr dataChannel;
        Message response; ZERO_MSG(&response);

        // establish that connection now
        dataChannel = Connection::connect(control_->remote_name(), clientCommand.port);

        std::stringstream lsData;
        fs::path p = fs::current_path();

#if WIN32
        for (const auto& item : fs::_Directory_iterator<true_type>(p)) {
#else
        for (const auto& item : fs::directory_iterator(p)) {
#endif
            if (fs::is_regular_file(item)) {
                lsData << item.path().filename() << endl;
            }
        }

        NetworkDataStream data(lsData);

        lsData.seekg(0, lsData.end);
        auto dataLen = lsData.tellg();

        lsData.seekg(0, lsData.beg); // rewind stream so it's ready for sending

        // let client know we're going to send data
        response = MAKE_MSG(MSGID::MESSAGE_OK, static_cast<uint32_t>(dataLen), 0, "");

        control_->send(response);

        // now send actual data
        if (dataChannel->send(data) != dataLen)
            throw ConnectionException(("failed to send all " + std::to_string(dataLen) + " bytes").c_str());

        dataChannel->shutdown();

        print_command_result(clientCommand, true);
    }
    catch (const ConnectionException& ce) {
        print_command_result(clientCommand, false, ce.what());
        throw ce; // we don't know how severe it is, so not our problem
                  // TODO: handle this more gracefully
    }
}


void ClientSession::handle_get(const Message& clientCommand) {
    try {
        Connection::Ptr dataChannel;
        Message response; MAKE_EMSG(MSGECODE::ERR_UNKNOWN);
        fstream input;
        fs::path filePath(clientCommand.payload);
        std::streampos fileSize = 0;

        // the client is waiting on an OK or ERROR message and has a socket ready for us

        // first check for failure scenarios:
        {
            // did they actually specify a filename?
            if (clientCommand.payload.empty()) {
                response = MAKE_EMSG(MSGECODE::ERR_INVALID_FILENAME);

            // did they try to use some kind of path (not allowed for now)
            } else if (clientCommand.payload.find('/') != std::string::npos || clientCommand.payload.find('\\') != std::string::npos) {
                response = MAKE_EMSG(MSGECODE::ERR_INVALID_FILENAME);
                std::stringstream stream;

                stream << "'" << clientCommand.payload << "' contains a path which is not permitted";
                response.payload = stream.str();
                
            // does their filename exist?
            } else if (!fs::exists(fs::path(clientCommand.payload))) {
                response = MAKE_EMSG(MSGECODE::ERR_DOES_NOT_EXIST);
                std::stringstream stream;

                stream << "File '" << clientCommand.payload << "' not found on server";
                response.payload = stream.str();

            // did they specify something that isn't a file?
            } else if (!fs::is_regular_file(filePath)) {
                response = MAKE_EMSG(MSGECODE::ERR_NOT_A_FILE);

                std::stringstream stream;

                stream << "'" << clientCommand.payload << "' is not a file";
                response.payload = stream.str();
            }

            // pass all the normal checks, now try to open the file
            else {
                input.open(filePath, fstream::binary | fstream::in | fstream::out);
                if (!input.good())
                    response = MAKE_EMSG(MSGECODE::ERR_FAILED_TO_OPEN);

                // find out number of bytes in the file
                input.seekg(0, input.end);
                fileSize = input.tellg();
                input.seekg(0, input.beg);

                // it's possible seek failed for some reason, and if we can't tell the size
                // of the file we're toast already
                if (!input.good()) {
                    response = MAKE_EMSG(MSGECODE::ERR_UNKNOWN);
                    response.payload = "could not determine length of file";
                }
            }
        }


        // if we have a failure response, we're done, command has failed
        if (response.msgid == MSGID::MESSAGE_ERROR) {
            print_command_result(clientCommand, false, response.payload);
            control_->send(response);
            return;
        }


        // everything looks good, connect to the client and let them know how much data
        // is coming
        dataChannel = Connection::connect(control_->remote_name(), clientCommand.port);

        response = MAKE_MSG(MSGID::MESSAGE_OK);
        response.datalen = fileSize;

        control_->send(response);

        // now actually stream the data over to the client. Connection handles splitting
        // this in chunks for us
        NetworkDataStream stream(input);

        const auto bytesSent = dataChannel->send(stream);

        dataChannel->shutdown();

        // that's it, just make sure we sent everything and let the data connection close
        if (bytesSent != fileSize) {
            print_command_result(clientCommand, false, "did not send all bytes; " + std::to_string(bytesSent) + " of " + std::to_string(fileSize) + " sent");
        } else print_command_result(clientCommand, true);

    }
    catch (const ConnectionException& ce) {
        print_command_result(clientCommand, false, ce.what());
    }
}


void ClientSession::handle_put(const Message& clientCommand) {
	Connection::Ptr dataChannel;
	Message response; MAKE_EMSG(MSGECODE::ERR_UNKNOWN);
	fstream output;
	fs::path filePath(clientCommand.payload);
	std::streampos fileSize = 0;


    // check for existence of file first: don't let client effectively delete stuff without
    // an explicit command (if it were to be implemented)
    if (fs::exists(filePath)) {
        response = MAKE_EMSG(MSGECODE::ERR_ALREADY_EXISTS);
    } 

    // don't allow invalid filenames:
    //  - empty
    //  - containing any kind of path separator (client could do bad things if we allowed this)
    else if (clientCommand.payload.empty() || clientCommand.payload.find('/') != string::npos || clientCommand.payload.find('\\') != string::npos) {
        response = MAKE_EMSG(MSGECODE::ERR_INVALID_FILENAME);
    }
    else {
        // now try to actually open the file
        output.open(filePath, output.binary | output.in | output.out | output.trunc);

        if (!output.is_open())
            response = MAKE_EMSG(MSGECODE::ERR_FAILED_TO_OPEN);
        else response = MAKE_MSG(MSGID::MESSAGE_OK); // all checks passed
    }

    // if there was some kind of error, report it to the client (we won't attempt
    // to establish a connection in this case)
    if (response.msgid != MSGID::MESSAGE_OK) {
        print_command_result(clientCommand, false, connection::strecode(response.ecode));
        
        control_->send(response);
        return;
    } 

    // no errors, everything is ready. Let the client know we're going to connect
    control_->send(response);
	dataChannel = Connection::connect(control_->remote_name(), clientCommand.port);

	//connection is created. receive data
	NetworkDataStream ds(output);

	auto bytesLeft = clientCommand.datalen;

	try {
		while (bytesLeft > 0) {
			// return value of received is guaranteed to be int or smaller
            // don't want to hold a bunch of data in memory at a time, so receive it a chunk at a time
            // and write out
			auto received = static_cast<uint64_t>(dataChannel->receive(ds, bytesLeft > CHUNK_SIZE ? CHUNK_SIZE : static_cast<int>(bytesLeft)));
			bytesLeft -= received;
    
            output.flush();
        }


        output.flush();

        // confirm we got what we expected
        const auto bytesReceived = clientCommand.datalen - bytesLeft;

        dataChannel->shutdown();

        if (bytesLeft == 0) {
            print_command_result(clientCommand, true);
        }
        else print_command_result(clientCommand, false, "Failed to receive " + clientCommand.payload + ": only received " + std::to_string(bytesReceived) + " of " + std::to_string(response.datalen) + " bytes");
		
	}
	catch (const ConnectionException& ce) { 
		print_command_result(clientCommand, false, ce.what());
	}
}


void ClientSession::handle_quit(const Message& clientCommand) {
    control_->shutdown();
    print_command_result(clientCommand, true);
}


void ClientSession::print_command_result(const Message& command, bool successful, const std::string& failureReason) {
    sync_cout.print(control_->identify_remote(), " : ", connection::to_string(command.msgid), successful ? " successful" : " FAILED - ", successful ? std::string() :
        (failureReason.size() > 0 ? failureReason : std::string("unknown reason")), sync_endl);
}
