# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)

foreach(required_var IN ITEMS GNEISS_ABI_BASELINE GNEISS_API_STABILITY_MANIFEST)
  if(NOT DEFINED ${required_var})
    message(FATAL_ERROR "缺少 API 稳定性检查参数：${required_var}")
  endif()
endforeach()

file(STRINGS "${GNEISS_ABI_BASELINE}" baseline_symbols ENCODING UTF-8)
file(STRINGS "${GNEISS_API_STABILITY_MANIFEST}" manifest_lines ENCODING UTF-8)

set(manifest_symbols)
set(stable_count 0)
set(experimental_count 0)
foreach(line IN LISTS manifest_lines)
  if("${line}" MATCHES "^[ \t]*#" OR "${line}" MATCHES "^[ \t]*$")
    continue()
  endif()
  if(NOT "${line}" MATCHES "^([^ ]+) (stable|experimental)$")
    message(FATAL_ERROR "无效的 API 稳定性清单行：${line}")
  endif()
  set(symbol "${CMAKE_MATCH_1}")
  set(level "${CMAKE_MATCH_2}")
  if(symbol IN_LIST manifest_symbols)
    message(FATAL_ERROR "API 稳定性清单包含重复符号：${symbol}")
  endif()
  list(APPEND manifest_symbols "${symbol}")
  if(level STREQUAL "stable")
    math(EXPR stable_count "${stable_count} + 1")
  else()
    math(EXPR experimental_count "${experimental_count} + 1")
  endif()
endforeach()

foreach(symbol IN LISTS baseline_symbols)
  if(NOT symbol IN_LIST manifest_symbols)
    message(FATAL_ERROR "ABI 基线符号缺少稳定性分类：${symbol}")
  endif()
endforeach()
foreach(symbol IN LISTS manifest_symbols)
  if(NOT symbol IN_LIST baseline_symbols)
    message(FATAL_ERROR "稳定性清单包含 ABI 基线外符号：${symbol}")
  endif()
endforeach()

list(LENGTH baseline_symbols baseline_count)
list(LENGTH manifest_symbols manifest_count)
if(NOT baseline_count EQUAL manifest_count)
  message(FATAL_ERROR "ABI 基线与稳定性清单数量不一致")
endif()

message(
  STATUS
    "API 稳定性清单通过：${manifest_count} 个符号，${stable_count} 个 Stable，${experimental_count} 个 Experimental"
)
