#pragma once
#include "Connection.h"
#include "Message.h"

typedef connection::Connection::Ptr ConnectionPtr;

class ClientSession {
    ConnectionPtr control_;

    bool greeting();

    void handle_ls(const connection::Message& clientCommand);
    void handle_get(const connection::Message& clientCommand);
    void handle_put(const connection::Message& clientCommand);
    void handle_quit(const connection::Message& clientCommand);

    void print_command_result(const connection::Message& command, bool successful, const std::string& failureReason = "");

    public:
        ClientSession(ConnectionPtr controlConnection);

        void serve();
};