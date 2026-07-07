/* Placeholder internal-flash bootloader linker script. Fill memory sizes during board bring-up. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}
