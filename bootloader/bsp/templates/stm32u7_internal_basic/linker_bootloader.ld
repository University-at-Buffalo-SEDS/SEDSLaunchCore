/* STM32U7 internal-flash template. Adjust RAM region for the exact package/SRAM banks. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 768K
}
