#pragma once
#include <stdint.h>
#include <string>
#include <iostream>
#include <sstream>

namespace connection {
    constexpr int MESSAGE_BYTE_LEN = 13;            // note: NOT sizeof! platform and packing differences can result in different sizeof()
    constexpr int MAX_PAYLOAD_LEN = 1024;          // set a limit to max payload, else it would be possible craft messages that consume all of a server's memory 
    constexpr int CHUNK_SIZE = 1024 * 32;          // read up to this many bytes at a time from receive buffer


    typedef uint16_t port_t;
    typedef uint16_t ecode_t;

    enum MSGID : uint8_t {
        MESSAGE_LS = 1,
        MESSAGE_GET,
        MESSAGE_PUT,
        MESSAGE_QUIT,

        MESSAGE_HELLO = 32,

        MESSAGE_OK = 128,
        MESSAGE_ERROR = 200
    };

    enum MSGECODE : uint16_t {
        ERR_UNKNOWN = 255,    
        ERR_FAILED_TO_OPEN = 1,
        ERR_DOES_NOT_EXIST,
        ERR_INVALID_FILENAME,
        ERR_NOT_A_FILE,
        ERR_ALREADY_EXISTS,
        ERR_UNRECOGNIZED_COMMAND 
    };


#define CASE_TO_STR(val) case val: return #val
#define CASE_TO_R_STR(val, msg) case val: return msg

    inline std::string to_string(const MSGECODE code) {
        switch (code) {
            CASE_TO_STR(ERR_UNKNOWN);
            CASE_TO_STR(ERR_FAILED_TO_OPEN);
            CASE_TO_STR(ERR_DOES_NOT_EXIST);
            CASE_TO_STR(ERR_INVALID_FILENAME);
            CASE_TO_STR(ERR_NOT_A_FILE);
            CASE_TO_STR(ERR_UNRECOGNIZED_COMMAND);
            CASE_TO_STR(ERR_ALREADY_EXISTS);
        default:
            return "unknown error";
        }
    }

    inline std::string strecode(const MSGECODE code) {
        switch (code) {
            CASE_TO_R_STR(ERR_FAILED_TO_OPEN, "failed to open the file");
            CASE_TO_R_STR(ERR_DOES_NOT_EXIST, "file does not exist");
            CASE_TO_R_STR(ERR_INVALID_FILENAME, "invalid filename");
            CASE_TO_R_STR(ERR_NOT_A_FILE, "not a file");
            CASE_TO_R_STR(ERR_UNRECOGNIZED_COMMAND, "command not recognized");
            CASE_TO_R_STR(ERR_ALREADY_EXISTS, "file already exists");

            default:
                return "no description";
        }
    }


    inline std::string to_string(const MSGID msg) {
        switch (msg) {
            CASE_TO_STR(MESSAGE_LS);
            CASE_TO_STR(MESSAGE_GET);
            CASE_TO_STR(MESSAGE_PUT);
            CASE_TO_STR(MESSAGE_QUIT);
            CASE_TO_STR(MESSAGE_HELLO);
            CASE_TO_STR(MESSAGE_OK);
            CASE_TO_STR(MESSAGE_ERROR);

            default:
                return "unknown message code";
        }
    }


#undef CASE_TO_STR

    struct Message {
        MSGID msgid;
        uint16_t msglen;
        uint64_t datalen;
        union {
            port_t port;
            MSGECODE ecode;
        };
        std::string payload;

        // use this to determine message length when payload has been set
        uint16_t length() const { return MESSAGE_BYTE_LEN + static_cast<uint16_t>(payload.length()); }

        friend std::ostream& operator<<(std::ostream& stream, const Message& message);

        bool operator==(const Message& other) {
            return (msgid == other.msgid && msglen == other.msglen && port == other.port && payload.compare(other.payload) == 0);
        }

        bool operator!=(const Message& other) {
            return !(*this == other);
        }

        std::string to_string() {
            std::ostringstream buf;

            buf << "STATUS CODE " << +(static_cast<int>(msgid)) << " " << connection::to_string(msgid) << std::endl;

            if (msgid == MSGID::MESSAGE_ERROR)
                buf << "\tError: " << connection::strecode(ecode) << std::endl;

            if (!payload.empty())
                buf << "\tResponse: " << payload << std::endl;

            return buf.str();
        }
    };




#define ZERO_MSG(msg) (msg)->msgid = MESSAGE_ERROR; (msg)->msglen = 0; (msg)->datalen = 0; (msg)->port = 0; (msg)->payload = "";

    inline Message MAKE_MSG(MSGID msgid, uint32_t datalen, port_t portOrEcode, const std::string& payload) {
        Message msg;

        msg.msgid = msgid;
        msg.datalen = datalen;
        msg.port = portOrEcode;
        msg.payload = payload;
        msg.msglen = msg.length();

        return msg;
    }

    inline Message MAKE_MSG(MSGID msgid, const std::string& payload = "") {
        return MAKE_MSG(msgid, 0, 0, payload);
    }

    inline Message MAKE_EMSG(MSGECODE code) {
        Message msg = MAKE_MSG(MSGID::MESSAGE_ERROR, "");

        msg.ecode = code;

        return msg;
    }

} // end connection namespace