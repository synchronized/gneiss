# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include_guard(GLOBAL)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/dependency_source_cache.cmake")

# fastgltf 原依赖同一版本的 singleheader；Apache-2.0/MIT，不进入普通公共 ABI。
function(gneiss_resolve_simdjson)
  if(TARGET simdjson::simdjson)
    return()
  endif()
  if(NOT FETCHCONTENT_SOURCE_DIR_GNEISS_SIMDJSON_SOURCE)
    gneiss_cached_archive(simdjson "https://codeload.github.com/simdjson/simdjson/tar.gz/v3.12.3"
      "d0af071f2f4187d8b26b556e83ef832b634bd5feb4e2f537b9dabbd334d4e334" CMakeLists.txt cached_source)
    FetchContent_Declare(gneiss_simdjson_source SOURCE_DIR "${cached_source}"
      SOURCE_SUBDIR gneiss-no-cmake)
  else()
    FetchContent_Declare(gneiss_simdjson_source SOURCE_SUBDIR gneiss-no-cmake)
  endif()
  FetchContent_MakeAvailable(gneiss_simdjson_source)
  add_library(gneiss_simdjson STATIC "${gneiss_simdjson_source_SOURCE_DIR}/singleheader/simdjson.cpp")
  add_library(simdjson::simdjson ALIAS gneiss_simdjson)
  target_include_directories(gneiss_simdjson SYSTEM PUBLIC "${gneiss_simdjson_source_SOURCE_DIR}/singleheader")
  target_compile_features(gneiss_simdjson PUBLIC cxx_std_17)
  set_target_properties(gneiss_simdjson PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
