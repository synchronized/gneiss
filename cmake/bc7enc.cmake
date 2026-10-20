# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/dependency_source_cache.cmake")

set(
  GNEISS_BC7ENC_GIT_REPOSITORY
  "https://github.com/richgel999/bc7enc_rdo.git"
  CACHE STRING
  "bc7enc_rdo Git 仓库"
)
set(
  GNEISS_BC7ENC_GIT_TAG
  "b9438627eef73a1157e84201b6fa6eb2ffd6d9f0"
  CACHE STRING
  "bc7enc_rdo 锁定提交"
)

function(gneiss_resolve_bc7enc)
  if(TARGET gneiss_bc7enc)
    return()
  endif()

  if(FETCHCONTENT_SOURCE_DIR_GNEISS_BC7ENC_SOURCE)
    FetchContent_Declare(gneiss_bc7enc_source SOURCE_SUBDIR gneiss-no-build)
  elseif(GNEISS_BC7ENC_GIT_REPOSITORY STREQUAL "https://github.com/richgel999/bc7enc_rdo.git"
      AND GNEISS_BC7ENC_GIT_TAG STREQUAL "b9438627eef73a1157e84201b6fa6eb2ffd6d9f0")
    gneiss_cached_archive(bc7enc
      "https://codeload.github.com/richgel999/bc7enc_rdo/tar.gz/b9438627eef73a1157e84201b6fa6eb2ffd6d9f0"
      "bbb33d1dbb6178a3a2c044956c3cc0b8ea9fc2f2903824379dd86f87d53b64c6" bc7enc.cpp cached_source)
    FetchContent_Declare(gneiss_bc7enc_source SOURCE_DIR "${cached_source}"
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/gneiss_bc7enc_source-build"
      SOURCE_SUBDIR gneiss-no-build)
  else()
    # 自定义仓库没有锁定归档摘要，保留原 Git 行为；离线需显式提供本地源码。
    if(GNEISS_DEPENDENCY_OFFLINE)
      message(FATAL_ERROR "自定义 bc7enc 离线构建需要 FETCHCONTENT_SOURCE_DIR_GNEISS_BC7ENC_SOURCE")
    endif()
    FetchContent_Declare(gneiss_bc7enc_source
      GIT_REPOSITORY "${GNEISS_BC7ENC_GIT_REPOSITORY}"
      GIT_TAG "${GNEISS_BC7ENC_GIT_TAG}" GIT_SHALLOW FALSE
      SOURCE_SUBDIR gneiss-no-build)
  endif()
  FetchContent_MakeAvailable(gneiss_bc7enc_source)

  add_library(
    gneiss_bc7enc STATIC
    "${gneiss_bc7enc_source_SOURCE_DIR}/bc7enc.cpp"
    "${gneiss_bc7enc_source_SOURCE_DIR}/bc7decomp.cpp"
  )
  target_include_directories(gneiss_bc7enc SYSTEM PUBLIC "${gneiss_bc7enc_source_SOURCE_DIR}")
  target_compile_features(gneiss_bc7enc PUBLIC cxx_std_11)
  set_target_properties(gneiss_bc7enc PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
