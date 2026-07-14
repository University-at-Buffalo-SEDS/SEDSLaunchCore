/* Placeholder internal-flash bootloader linker script. Fill memory sizes during board bring-up. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
  FLASH_APP_A (rx) : ORIGIN = 0x08020000, LENGTH = 512K
  FLASH_APP_B (rx) : ORIGIN = 0x080A0000, LENGTH = 512K
  FLASH_METADATA_0 (rw) : ORIGIN = 0x08120000, LENGTH = 8K
  FLASH_METADATA_1 (rw) : ORIGIN = 0x08122000, LENGTH = 8K
  FLASH_PERSIST (rw) : ORIGIN = 0x081E0000, LENGTH = 128K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}
