#include "launchcore/recovery.h"
#include "launchcore/storage.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    assert(launchcore_storage_init() == LAUNCHCORE_STORAGE_OK);

    char response[96];
    assert(launchcore_recovery_handle_command("HELLO", response, sizeof(response)) == LAUNCHCORE_RECOVERY_OK);
    assert(strcmp(response, "OK LAUNCHCORE") == 0);

    assert(launchcore_recovery_handle_command("GET_INFO", response, sizeof(response)) == LAUNCHCORE_RECOVERY_OK);
    assert(strcmp(response, "OK A=00000000/4194304 B=00400000/4194304") == 0);

    assert(launchcore_recovery_handle_command("GET_METADATA", response, sizeof(response)) == LAUNCHCORE_RECOVERY_OK);
    assert(strcmp(response, "OK status=1 seq=0 active=0 confirmed=255 pending=255") == 0);

    char truncated[5];
    assert(launchcore_recovery_handle_command("HELLO", truncated, sizeof(truncated)) == LAUNCHCORE_RECOVERY_OK);
    assert(strcmp(truncated, "OK L") == 0);

    assert(launchcore_recovery_handle_command("UNKNOWN", response, sizeof(response)) == LAUNCHCORE_RECOVERY_ERR_COMMAND);
    assert(strcmp(response, "ERR COMMAND") == 0);
    return 0;
}
