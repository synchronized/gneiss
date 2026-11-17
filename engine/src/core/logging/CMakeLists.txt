# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
include(spdlog)
gneiss_resolve_spdlog()
add_library(gneiss_file_log_sink STATIC file_sink.cpp)
target_include_directories(gneiss_file_log_sink PRIVATE "${PROJECT_SOURCE_DIR}/engine/include")
target_link_libraries(gneiss_file_log_sink PRIVATE gneiss_spdlog)
target_compile_features(gneiss_file_log_sink PRIVATE cxx_std_20)
set_target_properties(gneiss_file_log_sink PROPERTIES POSITION_INDEPENDENT_CODE ON)
gneiss_target_compile_warnings(gneiss_file_log_sink)

target_include_directories(gneiss_file_log_sink PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gneiss_file_log_sink PUBLIC gneiss_core_values)
