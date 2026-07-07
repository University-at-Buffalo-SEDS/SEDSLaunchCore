#include "launchcore/image.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t payload[8] = {1,2,3,4,5,6,7,8};
    launchcore_image_header_t h;
    memset(&h, 0, sizeof(h));
    h.magic = LAUNCHCORE_IMAGE_MAGIC;
    h.header_version = LAUNCHCORE_IMAGE_HEADER_VERSION;
    h.header_size = sizeof(h);
    h.image_size = sizeof(payload);
    h.image_crc32 = launchcore_crc32(payload, sizeof(payload));
    launchcore_sha256(payload, sizeof(payload), h.image_sha256);
    h.version_major = 1;
    h.slot_base_addr = 0x90000000u;
    h.vector_table_addr = 0x90000100u;
    h.entry_addr = 0x90000105u;
    h.header_crc32 = launchcore_image_header_crc(&h);
    assert(launchcore_image_validate_header(&h) == LAUNCHCORE_IMAGE_OK);
    assert(launchcore_image_validate(&h, payload) == LAUNCHCORE_IMAGE_OK);
    h.magic = 0u;
    assert(launchcore_image_validate_header(&h) == LAUNCHCORE_IMAGE_ERR_MAGIC);
    return 0;
}
