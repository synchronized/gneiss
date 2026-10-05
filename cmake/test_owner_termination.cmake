# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
if(NOT DEFINED GNEISS_OWNER_KINDS)
  set(GNEISS_OWNER_KINDS world application scene)
endif()
foreach(kind IN LISTS GNEISS_OWNER_KINDS)
  execute_process(COMMAND "${GNEISS_TEST_EXECUTABLE}" "${kind}"
                  RESULT_VARIABLE result TIMEOUT 20)
  if(NOT "${result}" STREQUAL "73")
    message(FATAL_ERROR "${kind} 跨线程析构未按约定终止：${result}")
  endif()
endforeach()
