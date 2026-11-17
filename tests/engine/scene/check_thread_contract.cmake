# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
foreach(mode IN ITEMS count flush)
  execute_process(COMMAND "${PROGRAM}" "${mode}" RESULT_VARIABLE status
    OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic TIMEOUT 30)
  if(NOT status STREQUAL "86" OR NOT diagnostic MATCHES "is_owner_thread")
    message(FATAL_ERROR "World 所属线程契约未触发 (${mode}): ${status} ${diagnostic}")
  endif()
endforeach()
