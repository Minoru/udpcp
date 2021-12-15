#include <array>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

enum class packet_type : std::uint8_t {
    ACK = 0,
    PUT = 1
};

const size_t PACKET_HEADER_SIZE = 17; // bytes
const size_t MAX_DATA_LEN = 1472; // bytes

struct packet_t {
    std::uint32_t seq_number;
    std::uint32_t seq_total;
    packet_type type;
    std::array<char, 8> id;
    std::array<char, MAX_DATA_LEN> data;
} __attribute__((packed));

void deserialize_packet(packet_t& packet) {
    packet.seq_number = ntohl(packet.seq_number);
    packet.seq_total = ntohl(packet.seq_total);
    // No need to convert packet.type as it's a single byte
    // No need to convert packet.id and packet.data as they're opaque arrays
}

int main(int argc, char** argv) {
    if (argc != 3) {
        const auto program_name = argv[0];
        std::cerr << "Usage: " << program_name << " address port\n";
        return EXIT_FAILURE;
    }

    const auto address = argv[1];
    const auto port = argv[2];

    struct ::addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    struct ::addrinfo* bind_address;
    const auto rc = ::getaddrinfo(address, port, &hints, &bind_address);
    if (rc != 0) {
        std::cerr << "Failed to parse address:port: " << gai_strerror(rc) << std::endl;
        return EXIT_FAILURE;
    }

    std::vector<int> listening_sockets;
    for (auto addr = bind_address; addr != nullptr; addr = addr->ai_next) {
        const int s = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (s == -1) {
            std::cerr << "Failed to create a socket: " << strerror(errno) << std::endl;
            continue;
        }

        if (::bind(s, addr->ai_addr, addr->ai_addrlen) == -1) {
            std::cerr << "Failed to bind the socket: " << strerror(errno) << std::endl;
            continue;
        }

        listening_sockets.push_back(s);
    }

    std::vector<pollfd> polled_sockets;
    for (const auto& sock : listening_sockets) {
        struct pollfd s;
        s.fd = sock;
        s.events = POLLIN;
        polled_sockets.push_back(s);
    }

    const int POLL_TIMEOUT_MS = 30000;
    int events_count;

    while (events_count = ::poll(polled_sockets.data(), polled_sockets.size(), POLL_TIMEOUT_MS), events_count != 0) {
        for (const pollfd sock : polled_sockets) {
            const bool is_ready = sock.revents & POLLIN;
            if (is_ready) {
                packet_t packet;
                const int bytes_read = ::recvfrom(sock.fd, static_cast<void*>(&packet), sizeof(packet_t), 0, nullptr, nullptr);
                deserialize_packet(packet);
                if (bytes_read == -1) {
                    std::cerr << "Failed to read data: " << strerror(errno) << std::endl;
                    continue;
                } else if (bytes_read < static_cast<int>(PACKET_HEADER_SIZE)) {
                    std::cerr << "Failed to read the packet header: expected " << PACKET_HEADER_SIZE << " bytes, got " << bytes_read << std::endl;
                    continue;
                } else {
                    std::cerr << "Got a packet!\n";
                    std::cerr << "\tseq_number = " << packet.seq_number << std::endl;
                    std::cerr << "\tseq_total  = " << packet.seq_total << std::endl;
                    std::cerr << "\ttype       = " << static_cast<int>(packet.type) << std::endl;
                    std::cerr << "\tid         = ";
                    for (const char x : packet.id) {
                        std::cerr << x;
                    }
                    std::cerr << std::endl;
                    std::cerr << bytes_read - PACKET_HEADER_SIZE << " bytes of data\n";
                }
            }
        }
    }

    ::freeaddrinfo(bind_address);
}
