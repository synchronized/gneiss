# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(gneiss_dynamic_library STATIC "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/dynamic_library/dynamic_library.hpp")
if(WIN32)
  target_sources(gneiss_dynamic_library PRIVATE dynamic_library_win32.cpp)
elseif(UNIX)
  target_sources(gneiss_dynamic_library PRIVATE dynamic_library_posix.cpp)
else()
  target_sources(gneiss_dynamic_library PRIVATE dynamic_library_stub.cpp)
endif()
target_link_libraries(gneiss_dynamic_library PUBLIC gneiss_core_values)
target_include_directories(gneiss_dynamic_library PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_dynamic_library PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_dynamic_library)
gneiss_target_output_directories(gneiss_dynamic_library)
