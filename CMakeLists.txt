# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)

project(
  gneiss
  VERSION 0.1.0
  DESCRIPTION "基于 Granit 构建的现代 C++20 游戏引擎"
  LANGUAGES C CXX
)

option(BUILD_SHARED_LIBS "构建共享库" ON)
option(GNEISS_BUILD_TESTING "构建 Gneiss 测试" ON)
option(GNEISS_BUILD_EXAMPLES "构建 Gneiss 示例" ${PROJECT_IS_TOP_LEVEL})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(utility)
include(CTest)

add_subdirectory(src)

if(GNEISS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(GNEISS_BUILD_TESTING AND BUILD_TESTING)
  add_subdirectory(tests)
endif()
