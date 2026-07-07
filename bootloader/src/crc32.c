#include "launchcore/image.h"

uint32_t launchcore_crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

uint32_t launchcore_crc32(const void *data, size_t len)
{
    return launchcore_crc32_update(0u, data, len);
}
