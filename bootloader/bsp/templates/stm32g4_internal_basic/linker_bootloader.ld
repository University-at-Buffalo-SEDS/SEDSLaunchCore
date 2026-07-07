/* STM32G4 internal-flash template. Adjust FLASH/RAM sizes for the exact part. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}
