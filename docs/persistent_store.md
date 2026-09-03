# Persistent Store

LaunchCore provides a bootloader-managed flash journal for settings that must
survive application and bootloader updates. Board layouts reserve
`persistent_data_base`, `persistent_data_size`, and
`persistent_data_erase_size`. Set `persistent_data_write_size` to the flash
programming unit required for record write addresses and lengths. The value must
be a power of two from 4 through 32 bytes, and `persistent_data_base` must be
aligned to it. Zero retains the legacy 4-byte default. The persistent range
must not overlap any application, staging, metadata, or bootloader erase range.

```c
launchcore_persist_init();
launchcore_persist_set(key, value, value_size);
launchcore_persist_get(key, value, &value_size);
launchcore_persist_delete(key);
```

Keys are 32-bit integers and values are at most
`LAUNCHCORE_PERSIST_MAX_VALUE_SIZE` bytes. Records carry CRC32 and sequence
numbers. The partition is split into two erase-aligned banks. Compaction copies
current records to the inactive bank and commits that bank only after every
copy completes, leaving the previous bank authoritative after an interruption.
Records are padded to `persistent_data_write_size` and written as one aligned
programming operation.

Applications should reserve a key for their schema version and migrate records
after a successful firmware update. This API is for settings and small state
records, not bulk logs or filesystem workloads.
