# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
if(NOT DEFINED GNEISS_SOURCE_DIR)
  message(FATAL_ERROR "缺少 GNEISS_SOURCE_DIR")
endif()
# 当前收口 Core、World 与 Application；后续逐模块扩大范围。
file(GLOB_RECURSE sources "${GNEISS_SOURCE_DIR}/src/core/*.cpp"
                          "${GNEISS_SOURCE_DIR}/src/core/*.h"
                          "${GNEISS_SOURCE_DIR}/src/core/*.hpp"
                          "${GNEISS_SOURCE_DIR}/src/world/*.cpp"
                          "${GNEISS_SOURCE_DIR}/src/world/*.h"
                          "${GNEISS_SOURCE_DIR}/src/world/*.hpp"
                          "${GNEISS_SOURCE_DIR}/src/application/*.cpp"
                          "${GNEISS_SOURCE_DIR}/src/application/*.h"
                          "${GNEISS_SOURCE_DIR}/src/application/*.hpp")
if(NOT sources)
  message(FATAL_ERROR "Core / World / Application 检查未找到源码，拒绝空检查")
endif()
foreach(source IN LISTS sources)
  file(READ "${source}" content)
  if(content MATCHES "extern[ \t]+\"C\"")
    message(FATAL_ERROR "Core / World / Application 不得定义 C ABI 入口：${source}")
  endif()
  if(content MATCHES "#[ \t]*include[ \t]*[<\"]gneiss/(application|world|scene|asset|render|reflection|game_module|gneiss)\\.hpp[>\"]")
    message(FATAL_ERROR "Core / World / Application 不得依赖公共 SDK 包装：${source}")
  endif()
  if(content MATCHES "gneiss_(world|scene_node|application)_[a-z_]+[ \t\r\n]*\\(")
    message(FATAL_ERROR "内部模块不得绕回 World / Application C ABI：${source}")
  endif()
endforeach()
