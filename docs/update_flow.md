# Update Flow

Applications call `launchcore_update_begin`, stream an already packaged
LaunchCore image with `launchcore_update_write`, then call
`launchcore_update_finish`. The update library chooses the inactive slot,
validates the written image, marks it pending, and lets the next reset perform
the boot decision.
