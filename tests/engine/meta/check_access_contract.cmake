# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
foreach(mode IN ITEMS empty-instance wrong-kind empty-write empty-source)
  execute_process(COMMAND "${PROGRAM}" "${mode}" RESULT_VARIABLE status
    ERROR_VARIABLE diagnostic TIMEOUT 10)
  if(mode STREQUAL "empty-instance" OR mode STREQUAL "empty-write")
    set(expression "type().kind() != type_kind::invalid")
  elseif(mode STREQUAL "empty-source")
    set(expression "source.type().kind() != type_kind::invalid")
  else()
    set(expression "output.kind() == type().scalar_kind()")
  endif()
  string(FIND "${diagnostic}" "${expression}" found)
  if(NOT "${status}" STREQUAL "86" OR found LESS 0)
    message(FATAL_ERROR "Meta 访问契约未按预期终止：${mode} ${status} ${diagnostic}")
  endif()
endforeach()
