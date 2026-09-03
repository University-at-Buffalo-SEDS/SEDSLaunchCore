# Boot Flow

Reset enters internal flash. LaunchCore initializes minimal hardware and reads
the newest valid metadata copy. A userspace update marks a completed transfer as
pending; the bootloader treats that state as a request to verify, not proof that
the image is valid. It rereads a pending image in bounded chunks and validates
its CRC32 and SHA-256 before jumping through the application vector table. A
failed pending image is marked bad and the confirmed slot is used as fallback.

Before application handoff, LaunchCore disables interrupts, stops SysTick,
disables and clears NVIC interrupts, and calls the board deinitializer. It then
sets VTOR and uses an assembly-only tail to switch to the application's MSP,
restore the reset-time enabled-interrupt state, and branch directly to the
application reset handler without returning through a C stack frame.
