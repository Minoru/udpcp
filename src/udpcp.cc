#include <cstring>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <tuple>
#include <unistd.h>
#include <vector>

#include "config.h"

struct addrinfo* parse_address_port(const char* address, const char* port) {
    struct addrinfo* result;

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    const auto rc = ::getaddrinfo(address, port, &hints, &result);
    if (rc != 0) {
        ERR("Failed to parse address:port: " << gai_strerror(rc));
        ::exit(EXIT_FAILURE);
    }

    return result;
}

std::tuple<int, struct addrinfo*> prepare_socket(struct addrinfo* server_addresses) {
    int server;
    for (struct addrinfo* server_address = server_addresses; server_address != nullptr; server_address = server_address->ai_next) {
        server = ::socket(server_address->ai_family, server_address->ai_socktype, server_address->ai_protocol);
        if (server != -1) {
            return std::make_tuple(server, server_address);
        }
    }

    ERR("Failed to obtain a socket to the server");
    ::exit(EXIT_FAILURE);
}

void serialize_packet(packet_t& packet) {
    packet.seq_number = htonl(packet.seq_number);
    packet.seq_total = htonl(packet.seq_total);
    // No need to convert packet.type as it's a single byte
    // No need to convert packet.id and packet.data as they're opaque arrays
}

size_t get_file_size(const char* filename) {
    struct stat result;
    if (::stat(filename, &result) == -1) {
        ERR("Failed to obtain the size of the file: " << strerror(errno));
        ::exit(EXIT_FAILURE);
    }

    // `st_size` is of type `off_t`, which is a signed integer. File sizes
    // can't be negative though, so it's safe to cast into `size_t` which is
    // unsigned.
    //
    // To make sure that the value will always fit, we run a static check.
    static_assert(
            sizeof(size_t) >= sizeof(off_t),
            "We expect `size_t` to be able to fit all non-negative values of `off_t`");
    return static_cast<size_t>(result.st_size);
}

std::vector<char> read_file(const char* filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        ERR("Failed to open the file for reading");
        ::exit(EXIT_FAILURE);
    }

    const auto filesize = get_file_size(filename);

    std::vector<char> data;
    data.resize(filesize, 0);
    file.read(data.data(), filesize);

    return data;
}

std::array<char, 8> random_file_id() {
    ::srand(::time(nullptr) + ::getpid());
    std::array<char, 8> result;
    for (auto& x : result) {
        x = ::rand();
    }
    return result;
}

std::tuple<packet_t, size_t> prepare_packet(
        size_t filesize,
        std::uint32_t seq_number,
        std::uint32_t chunks_count,
        const std::array<char, 8>& file_id,
        const std::vector<char>& data)
{
    packet_t packet;
    packet.seq_number = seq_number;
    packet.seq_total = chunks_count;
    packet.type = packet_type::PUT;
    packet.id = file_id;

    const auto offset = seq_number * MAX_DATA_LEN;
    const auto data_len = std::min(filesize - offset, MAX_DATA_LEN);
    std::copy(
            data.cbegin() + offset,
            data.cbegin() + offset + data_len,
            packet.data.begin());
    serialize_packet(packet);

    const auto packet_length = PACKET_HEADER_SIZE + data_len;

    return std::make_tuple(packet, packet_length);
}

void send_chunk(
        int server,
        struct addrinfo* server_address,
        const char* filename,
        const packet_t& packet,
        size_t packet_length,
        std::uint32_t seq_number)
{
    const auto packet_ptr = static_cast<const void*>(&packet);
    const auto sent_bytes = ::sendto(server, packet_ptr, packet_length, 0, server_address->ai_addr, server_address->ai_addrlen);
    if (sent_bytes == -1) {
        ERR("Failed to send chunk #" << seq_number << ": " << strerror(errno));
        ::exit(EXIT_FAILURE);
    } else if (static_cast<size_t>(sent_bytes) != packet_length) { // safe to cast because -1 is handled above
        // TODO: re-send this packet, as we only sent a part of it
    } else {
        ERR("<< (" << filename << ") Sent chunk #" << seq_number);
    }
}

int main(int argc, char** argv) {
    if (argc != 4) {
        const auto program_name = argv[0];
        ERR("Usage: " << program_name << " ADDRESS PORT FILE");
        return EXIT_FAILURE;
    }

    const auto address = argv[1];
    const auto port = argv[2];
    const auto filename = argv[3];

    struct addrinfo* server_addresses = parse_address_port(address, port);

    int server = -1;
    struct addrinfo* server_address;
    std::tie(server, server_address) = prepare_socket(server_addresses);

    const auto data = read_file(filename);
    const auto filesize = data.size();

    const auto chunks_count = (filesize + MAX_DATA_LEN - 1) / MAX_DATA_LEN;
    ERR(filename << " is " << filesize << " bytes long, so " << chunks_count << " chunks, the last one is " << (filesize % MAX_DATA_LEN) << " bytes long");

    const auto file_id = random_file_id();

    for (std::uint32_t seq_number = 0; seq_number < chunks_count; ++seq_number) {
        packet_t packet;
        size_t packet_size;
        std::tie(packet, packet_size) = prepare_packet(filesize, seq_number, chunks_count, file_id, data);
        send_chunk(server, server_address, filename, packet, packet_size, seq_number);
    }

    ::freeaddrinfo(server_addresses);
}
