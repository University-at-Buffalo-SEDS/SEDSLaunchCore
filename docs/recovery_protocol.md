# Recovery Protocol

Milestone 1 defines a small text command set: `HELLO`, `GET_INFO`,
`GET_METADATA`, `ERASE_SLOT`, `WRITE`, `READ`, `VERIFY_SLOT`, `SET_PENDING`,
and `RESET`. The current parser implements the metadata and slot-control
commands and leaves binary `WRITE`/`READ` transport framing to board bring-up.
