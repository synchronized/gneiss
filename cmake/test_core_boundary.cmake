# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
set(fixture "${GNEISS_BINARY_DIR}/core-boundary-fixture")
file(MAKE_DIRECTORY "${fixture}/src/core")
foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/application.hpp>")
  file(WRITE "${fixture}/src/core/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/core/probe.cpp" "#include <gneiss/core/result.h>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝共享值类型")
endif()
