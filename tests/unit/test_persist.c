#include "launchcore/persist.h"
#include "launchcore/storage.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    assert(launchcore_persist_init() == LAUNCHCORE_PERSIST_OK);

    uint32_t value = 0x12345678u;
    assert(launchcore_persist_set(10u, &value, sizeof(value)) == LAUNCHCORE_PERSIST_OK);
    uint32_t result = 0u;
    size_t result_size = sizeof(result);
    assert(launchcore_persist_get(10u, &result, &result_size) == LAUNCHCORE_PERSIST_OK);
    assert(result_size == sizeof(result));
    assert(result == value);

    uint8_t boolean_setting = 1u;
    assert(launchcore_persist_set(0x55474C57u, &boolean_setting,
        sizeof(boolean_setting)) == LAUNCHCORE_PERSIST_OK);
    assert(launchcore_persist_init() == LAUNCHCORE_PERSIST_OK);
    boolean_setting = 0u;
    result_size = sizeof(boolean_setting);
    assert(launchcore_persist_get(0x55474C57u, &boolean_setting, &result_size) ==
        LAUNCHCORE_PERSIST_OK);
    assert(result_size == sizeof(boolean_setting));
    assert(boolean_setting == 1u);

    value = 0xABCDEF01u;
    assert(launchcore_persist_set(10u, &value, sizeof(value)) == LAUNCHCORE_PERSIST_OK);
    result_size = sizeof(result);
    assert(launchcore_persist_get(10u, &result, &result_size) == LAUNCHCORE_PERSIST_OK);
    assert(result == value);

    uint8_t blob[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
    for (size_t i = 0; i < sizeof(blob); ++i) blob[i] = (uint8_t)i;
    for (uint32_t key = 100u; key < 700u; ++key) {
        blob[0] = (uint8_t)key;
        assert(launchcore_persist_set(key % 17u, blob, sizeof(blob)) == LAUNCHCORE_PERSIST_OK);
    }
    result_size = sizeof(blob);
    memset(blob, 0, sizeof(blob));
    assert(launchcore_persist_get(700u % 17u, blob, &result_size) == LAUNCHCORE_PERSIST_OK);

    assert(launchcore_persist_delete(10u) == LAUNCHCORE_PERSIST_OK);
    result_size = sizeof(result);
    assert(launchcore_persist_get(10u, &result, &result_size) == LAUNCHCORE_PERSIST_NOT_FOUND);
    return 0;
}
