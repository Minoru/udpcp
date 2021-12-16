#include <cstddef>

#include "crc32c.h"

std::uint32_t crc32c(std::uint32_t crc, const unsigned char *buf, std::size_t len)
{
    int k;
    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (k = 0; k < 8; k++)
        crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
    }
    return ~crc;
}

std::uint32_t crc32c(const std::vector<unsigned char>& data) {
    return crc32c(0, data.data(), data.size());
}
