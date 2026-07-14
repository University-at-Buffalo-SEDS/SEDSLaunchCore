#include "launchcore/metadata.h"
#include "launchcore/update.h"

launchcore_status_t launchcore_get_update_report(launchcore_update_report_t *report)
{
    if (report == NULL) return LAUNCHCORE_ERR_BAD_STATE;
    launchcore_metadata_t md;
    launchcore_metadata_status_t status = launchcore_metadata_read_latest(&md);
    if (status == LAUNCHCORE_METADATA_ERR_STORAGE) return LAUNCHCORE_ERR_FLASH;
    if (status != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_ERR_METADATA;
    report->kind = (launchcore_update_kind_t)md.update_kind;
    report->attempted = md.update_attempted != 0u;
    report->result = (launchcore_update_result_t)md.update_result;
    report->reason = (launchcore_update_reason_t)md.update_reason;
    return LAUNCHCORE_OK;
}
