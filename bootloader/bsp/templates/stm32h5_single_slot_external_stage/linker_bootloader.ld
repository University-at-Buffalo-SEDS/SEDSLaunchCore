/*
 * STM32H5 single-slot reference allocation (2 MiB internal flash):
 *   FLASH              bootloader code        0x08000000..0x0801FFFF (128 KiB)
 *   FLASH_METADATA_0   boot metadata copy 0   0x08020000..0x08021FFF (8 KiB)
 *   FLASH_METADATA_1   boot metadata copy 1   0x08022000..0x08023FFF (8 KiB)
 *   FLASH_PERSIST      persistent journal     0x08024000..0x08033FFF (64 KiB)
 *   FLASH_APP          application image      0x08034000..0x081FFFFF (1840 KiB)
 * External flash at 0x90000000 provides a 2 MiB application staging region.
 */
MEMORY
{
  FLASH (rx)             : ORIGIN = 0x08000000, LENGTH = 128K
  FLASH_METADATA_0 (rw)  : ORIGIN = 0x08020000, LENGTH = 8K
  FLASH_METADATA_1 (rw)  : ORIGIN = 0x08022000, LENGTH = 8K
  FLASH_PERSIST (rw)     : ORIGIN = 0x08024000, LENGTH = 64K
  FLASH_APP (rx)         : ORIGIN = 0x08034000, LENGTH = 1840K
  APP_STAGING (rw)       : ORIGIN = 0x90000000, LENGTH = 2048K
  RAM (rwx)              : ORIGIN = 0x20000000, LENGTH = 640K
}
