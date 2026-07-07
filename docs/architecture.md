# Architecture

LaunchCore keeps boot policy in a small internal-flash bootloader. Storage,
platform setup, and recovery transports are board ports. Image and metadata
formats are shared by the bootloader, update library, tools, and tests.
