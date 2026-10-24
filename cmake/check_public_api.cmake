# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
cmake_minimum_required(VERSION 3.23)
file(GLOB adapters "${GNEISS_SOURCE_DIR}/src/engine/api/*.cpp" "${GNEISS_SOURCE_DIR}/src/runtime/api/*.cpp")
set(definitions)
foreach(adapter IN LISTS adapters)
  file(READ "${adapter}" content)
  string(REGEX MATCHALL "extern[ \t\r\n]+\"C\"[ \t\r\n]+[^;{}(]*gneiss_[a-zA-Z0-9_]+[ \t\r\n]*\\(" entries "${content}")
  foreach(entry IN LISTS entries)
    string(REGEX MATCH "gneiss_[a-zA-Z0-9_]+[ \t\r\n]*\\($" symbol "${entry}")
    string(REGEX REPLACE "[ \t\r\n]*\\($" "" symbol "${symbol}")
    if(symbol IN_LIST definitions)
      message(FATAL_ERROR "重复公共 C 定义：${symbol}")
    endif()
    list(APPEND definitions "${symbol}")
  endforeach()
endforeach()
if(definitions)
  message(FATAL_ERROR "已撤销公共 C API，不得新增导出：${definitions}")
endif()

set(GNEISS_REQUIRE_REVIEWED_CPP_TYPES ON)
include("${CMAKE_CURRENT_LIST_DIR}/check_cpp_types.cmake")

# 已删除的对象包装不能从普通公共头重新引入。
foreach(header IN ITEMS gneiss/gneiss.hpp gneiss/engine/application.hpp gneiss/engine/game_module.hpp)
  if(EXISTS "${GNEISS_SOURCE_DIR}/include/${header}")
    message(FATAL_ERROR "已删除的 SDK 入口重新出现：${header}")
  endif()
endforeach()
file(GLOB_RECURSE value_headers "${GNEISS_SOURCE_DIR}/include/gneiss/*.hpp" "${GNEISS_SOURCE_DIR}/include/gneiss/*.inl")
if(value_headers)
  message(FATAL_ERROR "不得恢复公共 C++ SDK 或值头：${value_headers}")
endif()

# 安装头仅保留结果码常量与符号可见性宏，避免重新积累完整 SDK。
set(allowed_headers
  gneiss/engine/core/export.h
  gneiss/engine/core/result.h
)
file(GLOB_RECURSE installed_headers RELATIVE "${GNEISS_SOURCE_DIR}/include"
  "${GNEISS_SOURCE_DIR}/include/*")
list(SORT allowed_headers)
list(SORT installed_headers)
if(NOT "${installed_headers}" STREQUAL "${allowed_headers}")
  message(FATAL_ERROR "公共头超出内部构建所需的基础宏定义：${installed_headers}")
endif()
