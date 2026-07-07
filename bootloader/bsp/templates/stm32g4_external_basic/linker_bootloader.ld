/* STM32G4 external-flash template. Use this with a board-specific external memory interface. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 96K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}
