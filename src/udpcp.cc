#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <numeric>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <tuple>
#include <unistd.h>
#include <vector>

#include "config.h"
#include "crc32c.h"

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
            int yes = 1;
            if (::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
                ERR("Failed to set SO_REUSEADDR on the socket");
                // This might not be fatal if no previous servers are around, so carry on
            }

            return std::make_tuple(server, server_address);
        }
    }

    ERR("Failed to obtain a socket to the server");
    ::exit(EXIT_FAILURE);
}

void serialize_packet(packet_t& packet) {
    packet.payload.seq_number = htonl(packet.payload.seq_number);
    packet.payload.seq_total = htonl(packet.payload.seq_total);
    // No need to convert packet.payload.type as it's a single byte
    // No need to convert packet.payload.id and packet.payload.data as they're opaque arrays
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

std::vector<unsigned char> read_file(const char* filename) {
    std::basic_ifstream<char> file(filename, std::ios::binary);
    if (!file.is_open()) {
        ERR("Failed to open the file for reading");
        ::exit(EXIT_FAILURE);
    }

    const auto filesize = get_file_size(filename);

    std::vector<unsigned char> data;
    data.resize(filesize, 0);
    // It's safe to cast from `unsigned char*` to `char*` because the sizes of
    // the types are the same, and we're only going to interpret `unsigned
    // char` as "byte", not as a character
    file.read(reinterpret_cast<char*>(data.data()), filesize);

    return data;
}

file_id random_file_id() {
    ::srand(::time(nullptr) + ::getpid());
    file_id result;
    for (auto& x : result.raw) {
        x = ::rand();
    }
    return result;
}

packet_t prepare_packet(
        size_t filesize,
        std::uint32_t seq_number,
        std::uint32_t chunks_count,
        file_id id,
        const std::vector<unsigned char>& data)
{
    packet_t packet;
    packet.payload.seq_number = seq_number;
    packet.payload.seq_total = chunks_count;
    packet.payload.type = packet_type::PUT;
    packet.payload.id = id;

    const auto offset = seq_number * MAX_DATA_LEN;
    const auto data_len = std::min(filesize - offset, MAX_DATA_LEN);
    std::copy(
            data.cbegin() + offset,
            data.cbegin() + offset + data_len,
            packet.payload.data.begin());
    serialize_packet(packet);

    packet.length = PACKET_HEADER_SIZE + data_len;

    return packet;
}

void send_chunk(
        int server,
        struct addrinfo* server_address,
        const char* filename,
        const packet_t& packet,
        std::uint32_t seq_number)
{
    const auto packet_ptr = static_cast<const void*>(&packet.payload);
    ssize_t sent_bytes;
    do {
        sent_bytes = ::sendto(server, packet_ptr, packet.length, 0, server_address->ai_addr, server_address->ai_addrlen);
        if (sent_bytes == -1) {
            ERR("Failed to send chunk #" << seq_number << ": " << strerror(errno));
            ::exit(EXIT_FAILURE);
        } else if (static_cast<size_t>(sent_bytes) != packet.length) { // safe to cast because -1 is handled above
            continue;
        } else {
            ERR("<-- (" << filename << ", " << packet.payload.id.as_number << ") Sent chunk #" << seq_number);
            break;
        }
    } while (true);
}

packet_t wait_for_ack(int server) {
    struct pollfd sock;
    sock.fd = server;
    sock.events = POLLIN;

    const int POLL_TIMEOUT_MS = 1000;

    packet_t result;
    result.length = 0;

    int events_count;
    do {
        events_count = ::poll(&sock, 1, POLL_TIMEOUT_MS);

        if (events_count == -1) {
            if (errno == EINTR) {
                continue;
            }
        } else if (events_count == 0) {
            // poll timed out
        } else {
            // We only have one socket, and we handled the timeout, so we're sure some data is available.
            assert(sock.revents & POLLIN);
            const auto bytes_received = ::recvfrom(sock.fd, static_cast<void*>(&result.payload), sizeof(result.payload), 0, nullptr, nullptr);
            if (bytes_received == -1) {
                ERR("Failed to read ACK: " << strerror(errno));
            } else if (bytes_received < static_cast<int>(PACKET_HEADER_SIZE)) {
                ERR("Failed to read ACK header: expected " << PACKET_HEADER_SIZE << " bytes, got " << bytes_received);
            } else if (result.payload.type == packet_type::ACK) {
                result.length = bytes_received;
            }
        }

        break;
    } while (true);

    return result;
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
    const auto checksum = crc32c(data);
    const auto filesize = data.size();

    const auto chunks_count = std::max(1lu, (filesize + MAX_DATA_LEN - 1) / MAX_DATA_LEN);
    ERR(filename << " is " << filesize << " bytes long, so " << chunks_count << " chunks, the last one is " << (filesize % MAX_DATA_LEN) << " bytes long");

    const auto file_id = random_file_id();

    std::uint32_t server_checksum = 0;

    std::vector<std::uint32_t> seq_numbers;
    seq_numbers.resize(chunks_count, 0);
    std::iota(seq_numbers.begin(), seq_numbers.end(), 0);
    std::random_shuffle(seq_numbers.begin(), seq_numbers.end());

    for (const auto seq_number : seq_numbers) {
        const packet_t packet = prepare_packet(filesize, seq_number, chunks_count, file_id, data);
        do {
            send_chunk(server, server_address, filename, packet, seq_number);
            const auto ack = wait_for_ack(server);
            if (ack.length != 0) {
                if (ack.length > PACKET_HEADER_SIZE) {
                    auto server_checksum_ptr = reinterpret_cast<unsigned char*>(&server_checksum);
                    std::copy(ack.payload.data.cbegin(), ack.payload.data.cbegin() + sizeof(server_checksum), server_checksum_ptr);
                }

                break;
            }
        } while (true);

    }

    std::string status;
    if (server_checksum == checksum) {
        status = " OK ";
    } else {
        status = "FAIL";
    }
    ERR(
            status
            << " Sent "
            << filename
            << " (id "
            << file_id.as_number
            << "), "
            << filesize
            << " bytes, with CRC32 of 0x"
            << std::hex
            << checksum
            << "; got server CRC32 of 0x"
            << server_checksum);

    ::freeaddrinfo(server_addresses);
}
