# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
foreach(mode IN ITEMS pass assert debug panic)
  execute_process(COMMAND "${PROGRAM}" "${mode}" RESULT_VARIABLE status
    OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic TIMEOUT 20)
  if(mode STREQUAL "pass" OR (mode STREQUAL "debug" AND RELEASE_ASSERT))
    if(NOT status STREQUAL "0")
      message(FATAL_ERROR "断言求值语义错误 (${mode}): ${status} ${diagnostic}")
    endif()
  else()
    if(NOT status STREQUAL "86" OR NOT diagnostic MATCHES "gneiss_assert_probe")
      message(FATAL_ERROR "断言失败诊断错误 (${mode}): ${status} ${diagnostic}")
    endif()
    if(NOT mode STREQUAL "panic" AND NOT diagnostic MATCHES "42")
      message(FATAL_ERROR "缺少表达式诊断: ${diagnostic}")
    endif()
  endif()
endforeach()
