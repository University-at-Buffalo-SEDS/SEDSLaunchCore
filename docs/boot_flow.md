# Boot Flow

Reset enters internal flash. LaunchCore initializes minimal hardware, reads the
newest valid metadata copy, selects a pending or confirmed slot, validates its
image header and payload, enables XIP when required, deinitializes bootloader
state, and jumps through the application vector table.
