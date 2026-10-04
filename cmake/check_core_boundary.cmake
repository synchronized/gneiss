# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
if(NOT DEFINED GNEISS_SOURCE_DIR)
  message(FATAL_ERROR "缺少 GNEISS_SOURCE_DIR")
endif()
# 第一阶段仅收口 Core；其他模块由后续迁移扩大范围，不以白名单隐藏存量依赖。
file(GLOB_RECURSE sources "${GNEISS_SOURCE_DIR}/src/core/*.cpp"
                          "${GNEISS_SOURCE_DIR}/src/core/*.h"
                          "${GNEISS_SOURCE_DIR}/src/core/*.hpp")
if(NOT sources)
  message(FATAL_ERROR "Core 检查未找到源码，拒绝空检查")
endif()
foreach(source IN LISTS sources)
  file(READ "${source}" content)
  if(content MATCHES "extern[ \t]+\"C\"")
    message(FATAL_ERROR "Core 不得定义 C ABI 入口：${source}")
  endif()
  if(content MATCHES "#[ \t]*include[ \t]*[<\"]gneiss/(application|world|scene|asset|render|reflection|game_module|gneiss)\\.hpp[>\"]")
    message(FATAL_ERROR "Core 不得依赖公共 SDK 包装：${source}")
  endif()
endforeach()
