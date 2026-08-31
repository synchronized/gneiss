# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(gneiss_child_process STATIC child_process.h)
if(WIN32)
  target_sources(gneiss_child_process PRIVATE child_process_win32.cpp)
elseif(UNIX)
  target_sources(gneiss_child_process PRIVATE child_process_posix.cpp)
else()
  target_sources(gneiss_child_process PRIVATE child_process_stub.cpp)
endif()
target_link_libraries(gneiss_child_process PUBLIC gneiss::engine)
target_include_directories(gneiss_child_process PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_child_process PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_child_process)
gneiss_target_output_directories(gneiss_child_process)
