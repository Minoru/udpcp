#include <cstring>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "config.h"

void serialize_packet(packet_t& packet) {
    packet.seq_number = htonl(packet.seq_number);
    packet.seq_total = htonl(packet.seq_total);
    // No need to convert packet.type as it's a single byte
    // No need to convert packet.id and packet.data as they're opaque arrays
}

size_t get_file_size(const char* filename) {
    struct stat result;
    if (::stat(filename, &result) == -1) {
        std::cerr << "Failed to obtain the size of the file: " << strerror(errno) << std::endl;
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

int main(int argc, char** argv) {
    ::srand(::time(nullptr) + ::getpid());

    if (argc != 4) {
        const auto program_name = argv[0];
        std::cerr << "Usage: " << program_name << " ADDRESS PORT FILE\n";
        return EXIT_FAILURE;
    }

    const auto address = argv[1];
    const auto port = argv[2];
    const auto filename = argv[3];

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo* server_addresses;
    const auto rc = ::getaddrinfo(address, port, &hints, &server_addresses);
    if (rc != 0) {
        std::cerr << "Failed to parse address:port: " << gai_strerror(rc) << std::endl;
        return EXIT_FAILURE;
    }

    int server = -1;
    struct addrinfo* server_address;
    for (server_address = server_addresses; server_address != nullptr; server_address = server_address->ai_next) {
        server = ::socket(server_address->ai_family, server_address->ai_socktype, server_address->ai_protocol);
        if (server != -1) {
            break;
        }
    }
    if (server == -1 || server_address == nullptr) {
        std::cerr << "Failed to obtain a socket to the server\n";
        return EXIT_FAILURE;
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open the file for reading\n";
        return EXIT_FAILURE;
    }

    const auto filesize = get_file_size(filename);

    std::vector<char> data;
    data.resize(filesize, 0);
    file.read(data.data(), filesize);

    const auto chunks_count = (filesize + MAX_DATA_LEN - 1) / MAX_DATA_LEN;
    std::cerr << filesize << " = " << chunks_count << " chunks, the last one is " << (filesize % MAX_DATA_LEN) << " bytes long\n";

    std::array<char, 8> file_id;
    for (auto& x : file_id) {
        x = ::rand();
    }

    for (std::uint32_t seq_number = 0; seq_number < chunks_count; ++seq_number) {
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

        const auto packet_ptr = static_cast<void*>(&packet);
        const auto packet_length = PACKET_HEADER_SIZE + data_len;
        const auto sent_bytes = ::sendto(server, packet_ptr, packet_length, 0, server_address->ai_addr, server_address->ai_addrlen);
        if (sent_bytes == -1) {
            std::cerr << "Failed to send chunk #" << seq_number << ": " << strerror(errno) << std::endl;
            return EXIT_FAILURE;
        } else if (static_cast<size_t>(sent_bytes) != packet_length) { // safe to cast because -1 is handled above
            // TODO: re-sent this packet, as we only sent a partial value
        } else {
            std::cerr << "Sent chunk #" << seq_number << std::endl;
        }
    }

    ::freeaddrinfo(server_addresses);
}
