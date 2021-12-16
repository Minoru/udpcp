#ifndef UDPCP_CONFIG_H
#define UDPCP_CONFIG_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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

/// A unique 8-byte identifier of a file being transferred.
union file_id {
    std::uint64_t as_number;
    std::array<char, 8> raw;
};

template<>
struct std::hash<file_id> {
    std::size_t operator()(const file_id& id) const noexcept {
        return std::hash<std::uint64_t>()(id.as_number);
    }
};

inline bool operator==(const file_id& lhs, const file_id& rhs) {
    return lhs.as_number == rhs.as_number;
}

inline bool operator!=(const file_id& lhs, const file_id& rhs) {
    return !(lhs == rhs);
}

/// Data packet.
struct packet_t {
    struct {
        /// Sequence number.
        std::uint32_t seq_number;
        /// Total number of packets that comprise this file.
        std::uint32_t seq_total;
        /// Packet type.
        packet_type type;
        /// File ID (unique per file).
        file_id id;
        /// A chunk of the file.
        std::array<unsigned char, MAX_DATA_LEN> data;
    } payload __attribute__((packed));

    /// The number of bytes in the packet (including the header).
    std::uint32_t length;
};

#define ERR(msg) {\
    std::ostringstream line;\
    line << msg;\
    line << std::endl;\
    std::cerr << line.str(); }

#endif /* UDPCP_CONFIG_H */
