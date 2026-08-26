# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ENTT_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(entt EXCLUDE_FROM_ALL)

# yyjson 始终作为内部静态实现依赖，避免其 ABI 和运行时库传播给 Gneiss 使用者。
add_library(gneiss_yyjson STATIC EXCLUDE_FROM_ALL yyjson/src/yyjson.c)
target_include_directories(gneiss_yyjson PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/yyjson/src")
set_target_properties(
  gneiss_yyjson
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE YES
)
