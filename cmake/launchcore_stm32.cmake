function(launchcore_get_stm32_cpu out_var board_name)
  string(TOLOWER "${board_name}" board_lower)

  if(board_lower MATCHES "^stm32g4")
    set(cpu "cortex-m4")
  elseif(board_lower MATCHES "^stm32h5")
    set(cpu "cortex-m33")
  elseif(board_lower MATCHES "^stm32h7")
    set(cpu "cortex-m7")
  elseif(board_lower MATCHES "^stm32u7")
    set(cpu "cortex-m33")
  else()
    set(cpu "${LAUNCHCORE_CPU}")
  endif()

  if(cpu STREQUAL "")
    message(FATAL_ERROR "Unable to infer ARM CPU for LAUNCHCORE_BOARD='${board_name}'. Set -DLAUNCHCORE_CPU=<cortex-m...>.")
  endif()

  set(${out_var} "${cpu}" PARENT_SCOPE)
endfunction()

function(launchcore_apply_arm_options target_name cpu)
  target_compile_options(${target_name} PRIVATE
    -mcpu=${cpu}
    -mthumb
    -ffunction-sections
    -fdata-sections
  )
  get_target_property(target_type ${target_name} TYPE)
  if(NOT target_type STREQUAL "STATIC_LIBRARY")
    target_link_options(${target_name} PRIVATE
      -mcpu=${cpu}
      -mthumb
      -Wl,--gc-sections
    )
  endif()
endfunction()

set(_LAUNCHCORE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(_LAUNCHCORE_ROOT_DIR "${_LAUNCHCORE_CMAKE_DIR}/.." ABSOLUTE)

function(_launchcore_read_layout_define config_file define_name out_var)
  file(STRINGS "${config_file}" define_line
       REGEX "^#[ \t]*define[ \t]+${define_name}[ \t]+")
  list(LENGTH define_line define_count)
  if(NOT define_count EQUAL 1)
    message(FATAL_ERROR
      "${config_file} must define ${define_name} exactly once for linker generation")
  endif()
  list(GET define_line 0 define_expr)
  string(REGEX REPLACE
    "^#[ \t]*define[ \t]+${define_name}[ \t]+" "" define_expr "${define_expr}")
  string(REGEX REPLACE "([0-9A-Fa-fx]+)[uUlL]+" "\\1" define_expr "${define_expr}")
  string(REGEX REPLACE "/\\*.*\\*/" "" define_expr "${define_expr}")
  math(EXPR define_value "${define_expr}")
  set(${out_var} "${define_value}" PARENT_SCOPE)
endfunction()

# Generate both GNU ld scripts from the board_config.h memory layout. The BSP
# remains the single source of truth; applications no longer need checked-in
# copies of a linker script just to change partition addresses.
function(launchcore_generate_linker_scripts)
  set(one_value_args BSP_CONFIG LAYOUT_PREFIX OUTPUT_DIRECTORY VECTOR_ALIGNMENT
      OUT_BOOTLOADER_SCRIPT OUT_APPLICATION_SCRIPT)
  cmake_parse_arguments(LINKER "" "${one_value_args}" "" ${ARGN})
  foreach(required_arg BSP_CONFIG LAYOUT_PREFIX OUTPUT_DIRECTORY
                       OUT_BOOTLOADER_SCRIPT OUT_APPLICATION_SCRIPT)
    if(NOT LINKER_${required_arg})
      message(FATAL_ERROR "launchcore_generate_linker_scripts requires ${required_arg}")
    endif()
  endforeach()

  get_filename_component(layout_file "${LINKER_BSP_CONFIG}" ABSOLUTE)
  _launchcore_read_layout_define("${layout_file}" LAUNCHCORE_INTERNAL_FLASH_BASE lc_flash_base)
  _launchcore_read_layout_define("${layout_file}" LAUNCHCORE_INTERNAL_FLASH_SIZE lc_flash_size)
  _launchcore_read_layout_define("${layout_file}" LAUNCHCORE_INTERNAL_SRAM_BASE lc_ram_base)
  _launchcore_read_layout_define("${layout_file}" LAUNCHCORE_INTERNAL_SRAM_SIZE lc_ram_size)
  _launchcore_read_layout_define("${layout_file}" LAUNCHCORE_BOOTLOADER_SIZE lc_boot_size)
  _launchcore_read_layout_define("${layout_file}" ${LINKER_LAYOUT_PREFIX}_SLOT_A_BASE lc_slot_base)
  _launchcore_read_layout_define("${layout_file}" ${LINKER_LAYOUT_PREFIX}_SLOT_A_SIZE lc_slot_size)
  _launchcore_read_layout_define("${layout_file}" ${LINKER_LAYOUT_PREFIX}_VECTOR_TABLE lc_vector_table)

  math(EXPR lc_header_size "${lc_vector_table} - ${lc_slot_base}")
  math(EXPR lc_firmware_size "${lc_slot_size} - ${lc_header_size}")
  if(lc_header_size LESS 0 OR lc_firmware_size LESS_EQUAL 0)
    message(FATAL_ERROR "Invalid Slot A/vector-table layout in ${layout_file}")
  endif()
  if(NOT LINKER_VECTOR_ALIGNMENT)
    set(LINKER_VECTOR_ALIGNMENT 0x200)
  endif()
  math(EXPR lc_vector_remainder "${lc_vector_table} % ${LINKER_VECTOR_ALIGNMENT}")
  if(NOT lc_vector_remainder EQUAL 0)
    message(FATAL_ERROR
      "Vector table 0x${lc_vector_table} in ${layout_file} is not aligned to "
      "${LINKER_VECTOR_ALIGNMENT} bytes")
  endif()

  foreach(value flash_base flash_size ram_base ram_size boot_size slot_base
                slot_size vector_table header_size firmware_size)
    math(EXPR LC_${value} "${lc_${value}}" OUTPUT_FORMAT HEXADECIMAL)
  endforeach()
  file(MAKE_DIRECTORY "${LINKER_OUTPUT_DIRECTORY}")
  set(boot_script "${LINKER_OUTPUT_DIRECTORY}/launchcore_bootloader.ld")
  set(app_script "${LINKER_OUTPUT_DIRECTORY}/launchcore_application.ld")
  configure_file("${_LAUNCHCORE_CMAKE_DIR}/linker_bootloader.ld.in"
                 "${boot_script}" @ONLY)
  configure_file("${_LAUNCHCORE_CMAKE_DIR}/linker_application.ld.in"
                 "${app_script}" @ONLY)
  set(${LINKER_OUT_BOOTLOADER_SCRIPT} "${boot_script}" PARENT_SCOPE)
  set(${LINKER_OUT_APPLICATION_SCRIPT} "${app_script}" PARENT_SCOPE)
  # Retain the BSP SRAM capacity for the aggregate memory report. Both images
  # reuse the same SRAM at different times, so their RAM usage is reported
  # independently rather than summed.
  set(LAUNCHCORE_INTERNAL_RAM_SIZE "${lc_ram_size}" PARENT_SCOPE)
endfunction()

function(launchcore_add_memory_report)
  set(one_value_args NAME BOOTLOADER_TARGET APPLICATION_TARGET
      BOOTLOADER_BINARY FIRMWARE_BINARY TOTAL_FLASH_SIZE
      BOOTLOADER_CAPACITY FIRMWARE_CAPACITY RAM_CAPACITY)
  cmake_parse_arguments(REPORT "" "${one_value_args}" "" ${ARGN})
  set(required_args NAME BOOTLOADER_TARGET APPLICATION_TARGET
      BOOTLOADER_BINARY FIRMWARE_BINARY TOTAL_FLASH_SIZE
      BOOTLOADER_CAPACITY FIRMWARE_CAPACITY)
  foreach(required_arg IN LISTS required_args)
    if(NOT REPORT_${required_arg})
      message(FATAL_ERROR "launchcore_add_memory_report requires ${required_arg}")
    endif()
  endforeach()
  if(NOT REPORT_RAM_CAPACITY)
    set(REPORT_RAM_CAPACITY "${LAUNCHCORE_INTERNAL_RAM_SIZE}")
  endif()
  if(NOT REPORT_RAM_CAPACITY)
    message(FATAL_ERROR
      "launchcore_add_memory_report requires RAM_CAPACITY when linker scripts "
      "were not generated by launchcore_generate_linker_scripts")
  endif()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_custom_target(${REPORT_NAME} ALL
    COMMAND "${Python3_EXECUTABLE}" "${_LAUNCHCORE_ROOT_DIR}/tools/memory_report.py"
      --bootloader "${REPORT_BOOTLOADER_BINARY}"
      --firmware "${REPORT_FIRMWARE_BINARY}"
      --bootloader-elf "$<TARGET_FILE:${REPORT_BOOTLOADER_TARGET}>"
      --firmware-elf "$<TARGET_FILE:${REPORT_APPLICATION_TARGET}>"
      --size-tool "${CMAKE_SIZE}"
      --bootloader-capacity "${REPORT_BOOTLOADER_CAPACITY}"
      --firmware-capacity "${REPORT_FIRMWARE_CAPACITY}"
      --total-flash "${REPORT_TOTAL_FLASH_SIZE}"
      --ram-capacity "${REPORT_RAM_CAPACITY}"
    DEPENDS ${REPORT_BOOTLOADER_TARGET} ${REPORT_APPLICATION_TARGET}
    VERBATIM)
endfunction()
