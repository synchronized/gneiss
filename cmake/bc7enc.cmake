# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)

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

  FetchContent_Declare(
    gneiss_bc7enc_source
    GIT_REPOSITORY "${GNEISS_BC7ENC_GIT_REPOSITORY}"
    GIT_TAG "${GNEISS_BC7ENC_GIT_TAG}"
    GIT_SHALLOW FALSE
    SOURCE_SUBDIR gneiss-no-build
  )
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
