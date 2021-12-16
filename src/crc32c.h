#ifndef UDPCPD_CRC32C_H
#define UDPCPD_CRC32C_H

#include <cstdint>
#include <vector>

std::uint32_t crc32c(const std::vector<unsigned char>& data);

#endif /* UDPCPD_CRC32C_H */
