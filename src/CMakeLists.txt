# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(gneiss)
add_library(gneiss::gneiss ALIAS gneiss)
gneiss_target_output_directories(gneiss)

set(GNEISS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
set(GNEISS_GENERATED_VERSION_HEADER
    "${GNEISS_GENERATED_INCLUDE_DIR}/gneiss/core/version.h")

file(MAKE_DIRECTORY "${GNEISS_GENERATED_INCLUDE_DIR}/gneiss/core")
configure_file(
  "${PROJECT_SOURCE_DIR}/include/gneiss/core/version.h.in"
  "${GNEISS_GENERATED_VERSION_HEADER}"
  @ONLY
)

target_sources(
  gneiss
  PRIVATE
    core/service_registry.cpp
    core/service_registry.h
    core/rid_table.h
    result.cpp
    version.cpp
    world/system_scheduler.cpp
    world/system_scheduler.h
    world/world_api.cpp
    world/world_state.h
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
      "${PROJECT_SOURCE_DIR}/include"
      "${GNEISS_GENERATED_INCLUDE_DIR}"
    FILES
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/export.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/entity.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/entity.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/result.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/result.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/rid.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/rid.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/gneiss.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/gneiss.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/world.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/world.hpp"
      "${GNEISS_GENERATED_VERSION_HEADER}"
)

target_compile_features(gneiss PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss)
target_link_libraries(gneiss PRIVATE EnTT::EnTT)
target_include_directories(
  gneiss
  PRIVATE "${PROJECT_SOURCE_DIR}/src"
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${GNEISS_GENERATED_INCLUDE_DIR}>
)
target_compile_definitions(
  gneiss
  PRIVATE GNEISS_BUILDING_LIBRARY
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_STATIC_DEFINE>
)
set_target_properties(
  gneiss
  PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
