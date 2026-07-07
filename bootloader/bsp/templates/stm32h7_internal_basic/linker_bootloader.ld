/* STM32H7 internal-flash template. Adjust for the exact dual-bank layout. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
  RAM (rwx)  : ORIGIN = 0x24000000, LENGTH = 512K
}
