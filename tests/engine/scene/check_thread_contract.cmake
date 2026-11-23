# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
foreach(mode IN ITEMS count flush update duplicate frozen reentry)
  execute_process(COMMAND "${PROGRAM}" "${mode}" RESULT_VARIABLE status
    OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic TIMEOUT 30)
  set(expected "is_owner_thread")
  if(mode STREQUAL "duplicate")
    set(expected "none_of")
  elseif(mode STREQUAL "frozen")
    set(expected "systems_frozen_")
  elseif(mode STREQUAL "reentry")
    set(expected "updating_")
  endif()
  if(NOT status STREQUAL "86" OR NOT diagnostic MATCHES "${expected}")
    message(FATAL_ERROR "World 契约未触发 (${mode}): ${status} ${diagnostic}")
  endif()
endforeach()
