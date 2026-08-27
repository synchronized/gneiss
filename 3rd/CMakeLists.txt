# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ENTT_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(entt EXCLUDE_FROM_ALL)

# yyjson 作为对象直接并入 Gneiss，静态安装包无需传播内部解析器目标。
add_library(gneiss_yyjson OBJECT EXCLUDE_FROM_ALL yyjson/src/yyjson.c)
target_include_directories(gneiss_yyjson PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/yyjson/src")
set_target_properties(
  gneiss_yyjson
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE YES
)

# PNG 解码器作为对象直接并入 Gneiss，不向安装 package 传播第三方目标。
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/miniz_export.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/miniz_export.h"
  COPYONLY
)
add_library(
  gneiss_png
  OBJECT EXCLUDE_FROM_ALL
  libspng/spng/spng.c
  miniz/miniz.c
  miniz/miniz_tdef.c
  miniz/miniz_tinfl.c
)
target_include_directories(
  gneiss_png
  PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated"
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libspng/spng"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/miniz"
)
target_compile_definitions(
  gneiss_png
  PUBLIC SPNG_STATIC
  PRIVATE SPNG_USE_MINIZ MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO
)
set_target_properties(
  gneiss_png
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE YES
)
