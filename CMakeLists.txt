cmake_minimum_required(VERSION 3.20)
project(seds_launchcore C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(LAUNCHCORE_BOARD "stm32h7_extflash_example" CACHE STRING "Board port to build")
set(LAUNCHCORE_CPU "" CACHE STRING "ARM Cortex-M CPU for custom STM32 board ports, e.g. cortex-m4, cortex-m7, cortex-m33.")

include(CTest)

add_library(launchcore_core
  bootloader/src/app_install.c
  bootloader/src/boot_select.c
  bootloader/src/bootloader_update.c
  bootloader/src/bundle.c
  bootloader/src/delta.c
  bootloader/src/crc32.c
  bootloader/src/image_validate.c
  bootloader/src/metadata.c
  bootloader/src/persist.c
  bootloader/src/recovery.c
  bootloader/src/recovery_install.c
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
  update_lib/src/update_status.c
  update_lib/src/bundle_update.c
  update_lib/src/delta_update.c
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
set(LAUNCHCORE_BOARD_STORAGE_SOURCES)
foreach(optional_source storage_qspi.c storage_ospi.c storage_spi.c storage_external_flash.c storage_internal_flash.c external_ram.c)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LAUNCHCORE_BOARD_DIR}/${optional_source})
    list(APPEND LAUNCHCORE_BOARD_SOURCES ${LAUNCHCORE_BOARD_DIR}/${optional_source})
    if(optional_source MATCHES "^storage_")
      list(APPEND LAUNCHCORE_BOARD_STORAGE_SOURCES ${LAUNCHCORE_BOARD_DIR}/${optional_source})
    endif()
  endif()
endforeach()

add_executable(launchcore_memory_map EXCLUDE_FROM_ALL
  tools/print_memory_map.c
  ${LAUNCHCORE_BOARD_STORAGE_SOURCES}
)
target_include_directories(launchcore_memory_map PRIVATE bootloader/include ${LAUNCHCORE_BOARD_DIR})

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
  include(cmake/launchcore_stm32.cmake)
  launchcore_get_stm32_cpu(LAUNCHCORE_INFERRED_CPU "${LAUNCHCORE_BOARD}")
endif()

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

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
  foreach(arm_target
      launchcore_core
      launchcore_update_lib
      launchcore_bootloader.elf
      example_app_external_xip.elf
      example_app_internal_slot.elf)
    launchcore_apply_arm_options(${arm_target} ${LAUNCHCORE_INFERRED_CPU})
  endforeach()
endif()

if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME cmake_linker_generation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cmake_integration.py"
            "${CMAKE_CURRENT_SOURCE_DIR}")

  add_executable(test_image_header tests/unit/test_image_header.c)
  target_link_libraries(test_image_header PRIVATE launchcore_core)
  add_test(NAME image_header COMMAND test_image_header)

  add_executable(test_metadata tests/unit/test_metadata.c)
  target_link_libraries(test_metadata PRIVATE launchcore_core)
  add_test(NAME metadata COMMAND test_metadata)

  add_executable(test_slot_select tests/unit/test_slot_select.c)
  target_link_libraries(test_slot_select PRIVATE launchcore_core)
  add_test(NAME slot_select COMMAND test_slot_select)

  add_executable(test_streamed_update tests/unit/test_streamed_update.c)
  target_link_libraries(test_streamed_update PRIVATE launchcore_update_lib)
  add_test(NAME streamed_update COMMAND test_streamed_update)

  add_executable(test_streamed_bootloader_update tests/unit/test_streamed_bootloader_update.c)
  target_link_libraries(test_streamed_bootloader_update PRIVATE launchcore_core)
  add_test(NAME streamed_bootloader_update COMMAND test_streamed_bootloader_update)

  add_executable(test_persist tests/unit/test_persist.c)
  target_link_libraries(test_persist PRIVATE launchcore_core)
  add_test(NAME persist COMMAND test_persist)

  add_executable(test_persist_strict tests/unit/test_persist_strict.c)
  target_link_libraries(test_persist_strict PRIVATE launchcore_core)
  add_test(NAME persist_strict COMMAND test_persist_strict)

  add_executable(test_persist_strict_write tests/unit/test_persist_strict_write.c)
  target_link_libraries(test_persist_strict_write PRIVATE launchcore_core)
  add_test(NAME persist_strict_write COMMAND test_persist_strict_write)

  add_executable(test_persist_strict_repair tests/unit/test_persist_strict_repair.c)
  target_link_libraries(test_persist_strict_repair PRIVATE launchcore_core)
  add_test(NAME persist_strict_repair COMMAND test_persist_strict_repair)

  add_executable(test_recovery tests/unit/test_recovery.c)
  target_link_libraries(test_recovery PRIVATE launchcore_core)
  add_test(NAME recovery COMMAND test_recovery)

  add_executable(test_single_slot_install tests/unit/test_single_slot_install.c)
  target_link_libraries(test_single_slot_install PRIVATE launchcore_update_lib)
  add_test(NAME single_slot_install COMMAND test_single_slot_install)

  add_executable(test_delta_update tests/unit/test_delta_update.c)
  target_link_libraries(test_delta_update PRIVATE launchcore_update_lib)
  add_test(NAME delta_update COMMAND test_delta_update)

  # Unit assertions contain executable test operations. Release's NDEBUG must
  # never turn those checks/calls into a successful no-op. Keep production
  # libraries and example/firmware targets on their original profile flags.
  set(_launchcore_assertion_tests
      test_image_header test_metadata test_slot_select test_streamed_update
      test_streamed_bootloader_update test_persist test_persist_strict test_persist_strict_write
      test_recovery test_single_slot_install test_delta_update)
  foreach(_launchcore_test IN LISTS _launchcore_assertion_tests)
    if(MSVC)
      target_compile_options(${_launchcore_test} PRIVATE /UNDEBUG)
    elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
      target_compile_options(${_launchcore_test} PRIVATE -UNDEBUG)
    else()
      message(FATAL_ERROR
          "Unit tests require an assertion-enabled compiler configuration: "
          "add the compiler's test-target-only NDEBUG undefine option")
    endif()
  endforeach()
endif()
