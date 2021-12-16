#ifndef UDPCP_CONFIG_H
#define UDPCP_CONFIG_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>

/// Number of bytes in the packet's header.
const size_t PACKET_HEADER_SIZE = 17;

/// Maximum number of bytes of data that a single packet can carry.
const size_t MAX_DATA_LEN = 1472;

/// Packet types.
enum class packet_type : std::uint8_t {
    /// Acknowledgement of a previous PUT packet.
    ACK = 0,
    /// A packet that contains a chunk of a file.
    PUT = 1
};

/// Data packet.
struct packet_t {
    /// Sequence number.
    std::uint32_t seq_number;
    /// Total number of packets that comprise this file.
    std::uint32_t seq_total;
    /// Packet type.
    packet_type type;
    /// File ID (unique per file).
    std::array<char, 8> id;
    /// A chunk of the file.
    std::array<char, MAX_DATA_LEN> data;
} __attribute__((packed));

#define ERR(msg) {\
    std::ostringstream line;\
    line << msg;\
    line << std::endl;\
    std::cerr << line.str(); }

#endif /* UDPCP_CONFIG_H */
