#include <cstring>
#include <iostream>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

#include "config.h"

void deserialize_packet(packet_t& packet) {
    packet.seq_number = ntohl(packet.seq_number);
    packet.seq_total = ntohl(packet.seq_total);
    // No need to convert packet.type as it's a single byte
    // No need to convert packet.id and packet.data as they're opaque arrays
}

struct addrinfo* parse_address_port(const char* address, const char* port) {
    struct addrinfo* result;

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    const auto rc = ::getaddrinfo(address, port, &hints, &result);
    if (rc != 0) {
        std::cerr << "Failed to parse address:port: " << gai_strerror(rc) << std::endl;
        ::exit(EXIT_FAILURE);
    }

    return result;
}

std::vector<int> bind_sockets(struct addrinfo* addresses) {
    std::vector<int> result;

    for (auto addr = addresses; addr != nullptr; addr = addr->ai_next) {
        const int s = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (s == -1) {
            std::cerr << "Failed to create a socket: " << strerror(errno) << std::endl;
            continue;
        }

        if (::bind(s, addr->ai_addr, addr->ai_addrlen) == -1) {
            std::cerr << "Failed to bind the socket: " << strerror(errno) << std::endl;
            continue;
        }

        result.push_back(s);
    }

    return result;
}

std::vector<pollfd> socket_to_pollfd(const std::vector<int>& sockets) {
    std::vector<pollfd> result;

    for (const auto& sock : sockets) {
        struct pollfd s;
        s.fd = sock;
        s.events = POLLIN;
        result.push_back(s);
    }

    return result;
}

void handle_packet(const packet_t& packet, int bytes_read) {
    std::cerr << "Got a packet!\n";
    std::cerr << "\tseq_number = " << packet.seq_number << std::endl;
    std::cerr << "\tseq_total  = " << packet.seq_total << std::endl;
    std::cerr << "\ttype       = " << static_cast<int>(packet.type) << std::endl;
    const auto id = *reinterpret_cast<const std::uint64_t*>(packet.id.data());
    std::cerr << "\tid         = " << id << std::endl;
    std::cerr << bytes_read - PACKET_HEADER_SIZE << " bytes of data\n";
}

int main(int argc, char** argv) {
    if (argc != 3) {
        const auto program_name = argv[0];
        std::cerr << "Usage: " << program_name << " ADDRESS PORT\n";
        return EXIT_FAILURE;
    }

    const auto address = argv[1];
    const auto port = argv[2];

    struct addrinfo* bind_address = parse_address_port(address, port);
    const auto listening_sockets = bind_sockets(bind_address);
    auto polled_sockets = socket_to_pollfd(listening_sockets);

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
                    handle_packet(packet, bytes_read);
                }
            }
        }
    }

    ::freeaddrinfo(bind_address);
}
