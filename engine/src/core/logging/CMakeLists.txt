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

target_link_libraries(gneiss_logging_sinks PUBLIC gneiss_core)
