# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(gneiss_project_config STATIC
  include/gneiss/project/project_description.hpp
  src/project_description.cpp
)
add_library(gneiss::project_config ALIAS gneiss_project_config)
target_include_directories(gneiss_project_config
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(gneiss_project_config PUBLIC gneiss::engine PRIVATE gneiss_yyjson)
target_compile_features(gneiss_project_config PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_project_config)
gneiss_target_output_directories(gneiss_project_config)
