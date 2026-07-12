/* STM32H5 external-flash template. Configure XSPI and TrustZone policy during porting. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 640K
}
