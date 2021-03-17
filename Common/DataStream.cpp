#include "stdafx.h"
#include "NetworkDataStream.h"

using std::endl;

#ifndef WIN32
#include <endian.h>
#define htonll htobe64
#define ntohll be64toh
#endif

namespace connection {
    // Serialization and deserialization is based on swapping byte orders around ... it would be very easy
    // to create a problem if the size of a type is changed or is not expected
    // this should catch most errors of that kind
    static_assert(sizeof(short) == sizeof(uint16_t), "unexpected byte size of short");
    static_assert(sizeof(MSGID) == sizeof(uint8_t), "unexpected byte size of MSGID");
    static_assert(sizeof(MSGECODE) == sizeof(uint16_t), "unexpected by size of MSGECODE");
    static_assert(sizeof(long long) == sizeof(uint64_t), "unexpected byte size of long long");
    static_assert(sizeof(CHUNK_SIZE) <= sizeof(int), "chunk size is too large: socket functions have int return values");

    // The following functions are necessary to translate into correct byte order: single-byte values
    // needn't be translated, but certain values (uint16/32) do

    // useful for debugging
    //std::ostream& operator<<(std::ostream& stream, const Message& message) {
    //    stream << "Message:\n";
    //    stream << " msgid: " << message.msgid << endl;
    //    stream << " msglen: " << message.msglen << endl;

    //    stream << (message.msgid != MSGID::MESSAGE_ERROR ? " port: " : " ecode: ") <<
    //        (message.msgid != MSGID::MESSAGE_ERROR ? message.port : message.ecode) << endl;

    //    stream << " datalen: " << message.length() << endl;
    //    stream << " payload: " << message.payload << endl;

    //    return stream;
    //}

    std::ostream& operator<<(std::ostream& stream, const Message& message) {
        stream << "MESSAGE CODE " << +static_cast<uint8_t>(message.msgid);

        if (!message.payload.empty())
            stream << ": " << message.payload;

        return stream;
    }




    std::string NetworkDataStream::read_str(uint16_t len) {
        if (len == 0)
            return std::string();

        auto buf = std::unique_ptr<char[]>(new char[len]);

        stream_.read(buf.get(), len);

        return std::string(buf.get(), len);
    }


    void NetworkDataStream::write_str(const std::string& str) {
        if (str.length() == 0) return;

        stream_.write(str.c_str(), str.length());
    }



    NetworkDataStream& operator>>(NetworkDataStream& stream, MSGID& msg) {
        uint8_t temp;
        stream.stream_.read(reinterpret_cast<char*>(&temp), sizeof(temp));
        msg = static_cast<MSGID>(temp);

        return stream;
    }


    NetworkDataStream& operator>>(NetworkDataStream& stream, MSGECODE& ecode) {
        uint16_t temp;
        stream.stream_.read(reinterpret_cast<char*>(&temp), sizeof(temp));
        ecode = static_cast<MSGECODE>(temp);
        return stream;
    }


    NetworkDataStream& operator>>(NetworkDataStream& stream, uint8_t& data) {
        char buf;

        stream.stream_.read(&buf, sizeof(uint8_t));
        data = static_cast<uint8_t>(buf);

        return stream;
    }


    NetworkDataStream& operator>>(NetworkDataStream& stream, uint16_t& data) {
        char buf[2];

        stream.stream_.read(buf, sizeof(uint16_t));
        data = ntohs(*(reinterpret_cast<uint16_t*>(buf)));

        return stream;
    }


    NetworkDataStream& operator>>(NetworkDataStream& stream, uint32_t& data) {
        char buf[sizeof(uint32_t)];

        stream.stream_.read(buf, sizeof(uint32_t));

        data = ntohl(*(reinterpret_cast<uint32_t*>(buf)));
        return stream;
    }


    NetworkDataStream& operator>>(NetworkDataStream& stream, uint64_t& data) {
        char buf[sizeof(uint64_t)];

        stream.stream_.read(buf, sizeof(uint64_t));

        data = ntohll(*(reinterpret_cast<uint64_t*>(buf)));
        return stream;
    }



    NetworkDataStream& operator<<(NetworkDataStream& stream, const MSGID& msg) {
        const uint8_t msgcode = msg;
        stream << msgcode;
        return stream;
    }


    NetworkDataStream& operator<<(NetworkDataStream& stream, const MSGECODE& ecode) {
        ecode_t code = ecode;
        stream << code;

        return stream;
    }


    NetworkDataStream& operator<<(NetworkDataStream& stream, const uint8_t& data) {
        stream.stream_.write(reinterpret_cast<const char*>(&data), sizeof(uint8_t));

        return stream;
    }


    NetworkDataStream& operator<<(NetworkDataStream& stream, const uint16_t& data) {
        uint16_t network = htons(data);

        stream.stream_.write(reinterpret_cast<const char*>(&network), sizeof(uint16_t));

        return stream;
    }


    NetworkDataStream& operator<<(NetworkDataStream& stream, const uint32_t& data) {
        uint32_t network = htonl(data);

        stream.stream_.write(reinterpret_cast<const char*>(&network), sizeof(uint32_t));

        return stream;
    }


    NetworkDataStream& operator<<(NetworkDataStream& stream, const uint64_t& data) {
        uint64_t network = htonll(data);

        stream.stream_.write(reinterpret_cast<const char*>(&network), sizeof(uint64_t));

        return stream;
    }
} // end connection namespace
