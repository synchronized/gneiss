# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
set(fixture "${GNEISS_BINARY_DIR}/core-boundary-fixture")
file(MAKE_DIRECTORY "${fixture}/src/core" "${fixture}/src/world" "${fixture}/src/application")
# 清除上一次失败用例遗留的内容，保证反例可重复执行。
file(WRITE "${fixture}/src/world/probe.cpp" "#include <gneiss/render.h>\n")
file(WRITE "${fixture}/src/application/probe.cpp" "#include <gneiss/application.h>\n")
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

file(MAKE_DIRECTORY "${fixture}/src/world")
foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/world.hpp>"
    "auto result = gneiss_world_destroy(world);")
  file(WRITE "${fixture}/src/world/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "World 边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/world/probe.cpp" "#include <gneiss/render.h>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "World 边界检查错误拒绝共享值类型")
endif()

foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/application.hpp>"
    "auto result = gneiss_application_destroy(application);")
  file(WRITE "${fixture}/src/application/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "Application 边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/application/probe.cpp" "#include <gneiss/application.h>\n")
