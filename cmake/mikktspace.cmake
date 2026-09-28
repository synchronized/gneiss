# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include_guard(GLOBAL)
include(FetchContent)

function(gneiss_resolve_mikktspace)
  if(TARGET gneiss_mikktspace)
    return()
  endif()
  FetchContent_Declare(
    gneiss_mikktspace_source
    GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
    GIT_TAG 3e895b49d05ea07e4c2133156cfa94369e19e409
    SOURCE_SUBDIR gneiss-no-cmake
  )
  FetchContent_MakeAvailable(gneiss_mikktspace_source)
  add_library(gneiss_mikktspace STATIC "${gneiss_mikktspace_source_SOURCE_DIR}/mikktspace.c")
  target_include_directories(gneiss_mikktspace SYSTEM PUBLIC "${gneiss_mikktspace_source_SOURCE_DIR}")
  set_target_properties(gneiss_mikktspace PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
