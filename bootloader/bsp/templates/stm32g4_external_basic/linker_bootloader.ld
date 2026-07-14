/* STM32G4 external-flash template. Use this with a board-specific external memory interface. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 96K
  EXT_APP_A (rx) : ORIGIN = 0x90000000, LENGTH = 3072K
  EXT_APP_B (rx) : ORIGIN = 0x90300000, LENGTH = 3072K
  EXT_PERSIST (rw) : ORIGIN = 0x90600000, LENGTH = 128K
  EXT_METADATA_0 (rw) : ORIGIN = 0x907E0000, LENGTH = 4K
  EXT_METADATA_1 (rw) : ORIGIN = 0x907F0000, LENGTH = 4K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}
