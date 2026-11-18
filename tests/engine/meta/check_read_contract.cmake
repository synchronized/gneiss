# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
foreach(mode IN ITEMS empty-instance wrong-kind)
  execute_process(COMMAND "${PROGRAM}" "${mode}" RESULT_VARIABLE status
    ERROR_VARIABLE diagnostic TIMEOUT 10)
  if(mode STREQUAL "empty-instance")
    set(expression "kind() != type_kind::invalid")
  else()
    set(expression "output.kind() == accessor->kind")
  endif()
  string(FIND "${diagnostic}" "${expression}" found)
  if(NOT "${status}" STREQUAL "86" OR found LESS 0)
    message(FATAL_ERROR "Meta 读取契约未按预期终止：${mode} ${status} ${diagnostic}")
  endif()
endforeach()
