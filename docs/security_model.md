# Security Model

Milestone 1 enforces CRC32 and SHA-256 image integrity and metadata CRCs. The
image header reserves signature storage. Production builds should add signature
verification and reject unsigned firmware.
