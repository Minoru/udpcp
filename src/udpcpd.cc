#include <cstring>
#include <iostream>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

#include "config.h"

void deserialize_packet(packet_t& packet) {
    packet.payload.seq_number = ntohl(packet.payload.seq_number);
    packet.payload.seq_total = ntohl(packet.payload.seq_total);
    // No need to convert packet.payload.type as it's a single byte
    // No need to convert packet.payload.id and packet.payload.data as they're opaque arrays
}

struct addrinfo* parse_address_port(const char* address, const char* port) {
    struct addrinfo* result;

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    const auto rc = ::getaddrinfo(address, port, &hints, &result);
    if (rc != 0) {
        ERR("Failed to parse address:port: " << gai_strerror(rc));
        ::exit(EXIT_FAILURE);
    }

    return result;
}

std::vector<int> bind_sockets(struct addrinfo* addresses) {
    std::vector<int> result;

    for (auto addr = addresses; addr != nullptr; addr = addr->ai_next) {
        const int s = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (s == -1) {
            ERR("Failed to create a socket: " << strerror(errno));
            continue;
        }

        if (::bind(s, addr->ai_addr, addr->ai_addrlen) == -1) {
            ERR("Failed to bind the socket: " << strerror(errno));
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

packet_t handle_packet(const packet_t& packet) {
    const auto id = *reinterpret_cast<const std::uint64_t*>(packet.payload.id.data());
    ERR("-->"
        << "\tseq_number = " << packet.payload.seq_number
        << "\tseq_total = " << packet.payload.seq_total
        << "\ttype = " << static_cast<int>(packet.payload.type)
        << "\tid = " << id
        << "\tand " << packet.length - PACKET_HEADER_SIZE << " bytes of data");

    packet_t ack;
    ack.payload.seq_number = packet.payload.seq_number;
    ack.payload.seq_total = packet.payload.seq_number; // TODO: return the actual count of received packets
    ack.payload.type = packet_type::ACK;
    ack.payload.id = packet.payload.id;
    ack.length = PACKET_HEADER_SIZE;
    return ack;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        const auto program_name = argv[0];
        ERR("Usage: " << program_name << " ADDRESS PORT");
        return EXIT_FAILURE;
    }

    const auto address = argv[1];
    const auto port = argv[2];

    struct addrinfo* bind_address = parse_address_port(address, port);
    const auto listening_sockets = bind_sockets(bind_address);
    auto polled_sockets = socket_to_pollfd(listening_sockets);

    const int POLL_TIMEOUT_MS = 5000;
    int events_count;

    while (events_count = ::poll(polled_sockets.data(), polled_sockets.size(), POLL_TIMEOUT_MS), events_count != 0) {
        for (const pollfd sock : polled_sockets) {
            const bool is_ready = sock.revents & POLLIN;
            if (!is_ready) {
                continue;
            }

            packet_t packet;
            struct sockaddr src_addr;
            socklen_t src_addrlen = sizeof(src_addr);
            const auto bytes_received = ::recvfrom(sock.fd, static_cast<void*>(&packet.payload), sizeof(packet.payload), 0, &src_addr, &src_addrlen);
            if (bytes_received == -1) {
                ERR("Failed to read data: " << strerror(errno));
                continue;
            } else if (bytes_received < static_cast<int>(PACKET_HEADER_SIZE)) {
                ERR("Failed to read the packet header: expected " << PACKET_HEADER_SIZE << " bytes, got " << bytes_received);
                continue;
            } else {
                // We handled negative case above, so we're sure the value is non-negative and can be casted into an unsigned type
                packet.length = static_cast<std::uint32_t>(bytes_received);
                deserialize_packet(packet);
                const auto ack = handle_packet(packet);
                const auto bytes_sent = ::sendto(sock.fd, static_cast<const void*>(&ack.payload), sizeof(ack.payload), 0, &src_addr, src_addrlen);
                if (bytes_sent == -1) {
                    ERR("Failed to send an ACK: " << strerror(errno));
                    continue;
                }
            }
        }
    }

    ::freeaddrinfo(bind_address);
}
