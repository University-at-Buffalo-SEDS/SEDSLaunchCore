cmake_minimum_required(VERSION 3.20)
project(seds_launchcore C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(LAUNCHCORE_BOARD "stm32h7_extflash_example" CACHE STRING "Board port to build")

include(CTest)

add_library(launchcore_core
  bootloader/src/boot_select.c
  bootloader/src/bootloader_update.c
  bootloader/src/crc32.c
  bootloader/src/image_validate.c
  bootloader/src/metadata.c
  bootloader/src/recovery.c
  bootloader/src/sha256.c
  bootloader/src/storage.c
)
target_include_directories(launchcore_core PUBLIC bootloader/include)

add_library(launchcore_update_lib
  update_lib/src/update_begin.c
  update_lib/src/update_write.c
  update_lib/src/update_finish.c
  update_lib/src/update_abort.c
  update_lib/src/confirm_boot.c
)
target_include_directories(launchcore_update_lib PUBLIC update_lib/include bootloader/include)
target_link_libraries(launchcore_update_lib PUBLIC launchcore_core)

set(LAUNCHCORE_BOARD_PORT_DIR bootloader/boards/${LAUNCHCORE_BOARD})
set(LAUNCHCORE_BSP_TEMPLATE_DIR bootloader/bsp/templates/${LAUNCHCORE_BOARD})
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LAUNCHCORE_BOARD_PORT_DIR}/platform.c)
  set(LAUNCHCORE_BOARD_DIR ${LAUNCHCORE_BOARD_PORT_DIR})
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LAUNCHCORE_BSP_TEMPLATE_DIR}/platform.c)
  set(LAUNCHCORE_BOARD_DIR ${LAUNCHCORE_BSP_TEMPLATE_DIR})
else()
  message(FATAL_ERROR "Unknown LAUNCHCORE_BOARD='${LAUNCHCORE_BOARD}'. Expected bootloader/boards/<name> or bootloader/bsp/templates/<name>.")
endif()
set(LAUNCHCORE_BOARD_SOURCES ${LAUNCHCORE_BOARD_DIR}/platform.c)
foreach(optional_source storage_qspi.c storage_ospi.c storage_spi.c storage_external_flash.c storage_internal_flash.c external_ram.c)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LAUNCHCORE_BOARD_DIR}/${optional_source})
    list(APPEND LAUNCHCORE_BOARD_SOURCES ${LAUNCHCORE_BOARD_DIR}/${optional_source})
  endif()
endforeach()

add_executable(launchcore_bootloader.elf
  bootloader/src/main.c
  bootloader/src/boot_jump.c
  ${LAUNCHCORE_BOARD_SOURCES}
)
target_include_directories(launchcore_bootloader.elf PRIVATE
  bootloader/include
  ${LAUNCHCORE_BOARD_DIR}
)
target_link_libraries(launchcore_bootloader.elf PRIVATE launchcore_core)

add_executable(example_app_external_xip.elf examples/app_external_xip/src/main.c)
target_include_directories(example_app_external_xip.elf PRIVATE update_lib/include)
target_link_libraries(example_app_external_xip.elf PRIVATE launchcore_update_lib)

add_executable(example_app_internal_slot.elf examples/app_internal_slot/src/main.c)
target_include_directories(example_app_internal_slot.elf PRIVATE update_lib/include)
target_link_libraries(example_app_internal_slot.elf PRIVATE launchcore_update_lib)

if(BUILD_TESTING)
  add_executable(test_image_header tests/unit/test_image_header.c)
  target_link_libraries(test_image_header PRIVATE launchcore_core)
  add_test(NAME image_header COMMAND test_image_header)

  add_executable(test_metadata tests/unit/test_metadata.c)
  target_link_libraries(test_metadata PRIVATE launchcore_core)
  add_test(NAME metadata COMMAND test_metadata)

  add_executable(test_slot_select tests/unit/test_slot_select.c)
  target_link_libraries(test_slot_select PRIVATE launchcore_core)
  add_test(NAME slot_select COMMAND test_slot_select)
endif()
