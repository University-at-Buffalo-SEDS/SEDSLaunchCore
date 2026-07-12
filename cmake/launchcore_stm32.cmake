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
      --specs=nosys.specs
      -Wl,--gc-sections
    )
  endif()
endfunction()
