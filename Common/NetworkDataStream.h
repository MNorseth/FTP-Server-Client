#pragma once
#include <iostream>
#include <sstream>
#include <stdint.h>
#include <string>
#include <memory>
#include "Message.h"

namespace connection {

    // this class handles correct translation between network-order bytes and host machine byte order
    class NetworkDataStream {
        std::iostream& stream_;

        public:
            friend class Connection;

            NetworkDataStream(std::iostream& str) : stream_(str) {}

            std::string read_str(uint16_t len);
            void write_str(const std::string& str);

            // note: do not use extraction/insertion operators for strings: this would require
            // inserting an extra 2 bytes for length that would be "hidden", making determining
            // length of a message less obvious

            friend NetworkDataStream& operator>>(NetworkDataStream& stream, MSGID& msg);
            friend NetworkDataStream& operator>>(NetworkDataStream& stream, MSGECODE& ecode);
            friend NetworkDataStream& operator>>(NetworkDataStream& stream, uint8_t& msg);
            friend NetworkDataStream& operator>>(NetworkDataStream& stream, uint16_t& msg);
            friend NetworkDataStream& operator>>(NetworkDataStream& stream, uint32_t& msg);
            friend NetworkDataStream& operator>>(NetworkDataStream& stream, uint64_t& msg);

            friend NetworkDataStream& operator<<(NetworkDataStream& stream, const MSGID& msg);
            friend NetworkDataStream& operator<<(NetworkDataStream& stream, const MSGECODE& ecode);
            friend NetworkDataStream& operator<<(NetworkDataStream& stream, const uint8_t& msg);
            friend NetworkDataStream& operator<<(NetworkDataStream& stream, const uint16_t& msg);
            friend NetworkDataStream& operator<<(NetworkDataStream& stream, const uint32_t& msg);
            friend NetworkDataStream& operator<<(NetworkDataStream& stream, const uint64_t& msg);
    };

} // end connection namespace