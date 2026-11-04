# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include_guard(GLOBAL)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/dependency_source_cache.cmake")

function(gneiss_resolve_mikktspace)
  if(TARGET gneiss_mikktspace)
    return()
  endif()
  if(NOT FETCHCONTENT_SOURCE_DIR_GNEISS_MIKKTSPACE_SOURCE)
    gneiss_cached_archive(mikktspace
      "https://codeload.github.com/mmikk/MikkTSpace/tar.gz/3e895b49d05ea07e4c2133156cfa94369e19e409"
      "aeba65ddee85a679133d510d71d00b108f603752f90b15b9ecf7777033f6e351" mikktspace.c cached_source)
    FetchContent_Declare(gneiss_mikktspace_source SOURCE_DIR "${cached_source}"
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/gneiss_mikktspace_source-build"
      SOURCE_SUBDIR gneiss-no-cmake)
  else()
    FetchContent_Declare(gneiss_mikktspace_source
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/gneiss_mikktspace_source-build"
      SOURCE_SUBDIR gneiss-no-cmake)
  endif()
  FetchContent_MakeAvailable(gneiss_mikktspace_source)
  add_library(gneiss_mikktspace STATIC "${gneiss_mikktspace_source_SOURCE_DIR}/mikktspace.c")
  target_include_directories(gneiss_mikktspace SYSTEM PUBLIC "${gneiss_mikktspace_source_SOURCE_DIR}")
  set_target_properties(gneiss_mikktspace PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
