# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
include(spdlog)
gneiss_resolve_spdlog()
add_library(gneiss_logging_sinks STATIC sinks.cpp)
target_include_directories(gneiss_logging_sinks PRIVATE "${PROJECT_SOURCE_DIR}/engine/include")
target_link_libraries(gneiss_logging_sinks PRIVATE gneiss_spdlog)
target_compile_features(gneiss_logging_sinks PRIVATE cxx_std_20)
set_target_properties(gneiss_logging_sinks PROPERTIES POSITION_INDEPENDENT_CODE ON)
gneiss_target_compile_warnings(gneiss_logging_sinks)

target_link_libraries(gneiss_logging_sinks PUBLIC gneiss_base)

find_package(Threads REQUIRED)
add_library(gneiss_logging service.cpp logger.cpp sink_connection.cpp default_service.cpp
  detail/async_queue.cpp)
add_library(gneiss::logging ALIAS gneiss_logging)
target_link_libraries(gneiss_logging PUBLIC gneiss_base PRIVATE gneiss_libassert Threads::Threads)
target_compile_definitions(gneiss_logging PRIVATE GNEISS_BUILDING_LOGGING
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_LOGGING_STATIC_DEFINE>)
set_target_properties(gneiss_logging PROPERTIES EXPORT_NAME logging POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
gneiss_target_compile_warnings(gneiss_logging)
gneiss_target_output_directories(gneiss_logging)
