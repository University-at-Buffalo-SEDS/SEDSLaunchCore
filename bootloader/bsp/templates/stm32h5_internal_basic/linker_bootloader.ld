/* STM32H5 internal-flash template. Adjust FLASH/RAM sizes and TrustZone split for the exact part. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 640K
}
