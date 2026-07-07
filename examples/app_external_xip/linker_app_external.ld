/* Slot A vector table starts after the LaunchCore header. */
MEMORY
{
  XFLASH (rx) : ORIGIN = 0x90000100, LENGTH = 4096K - 256
  RAM (rwx)  : ORIGIN = 0x24000000, LENGTH = 512K
  EXTRAM (rwx) : ORIGIN = 0xC0000000, LENGTH = 8192K
}
