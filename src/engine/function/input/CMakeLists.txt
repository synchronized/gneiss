# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 可选私有集成目标；暂由独立宿主链接，不同时链接 Engine DLL。
add_library(gneiss_caors_input STATIC
  ../../asset/caors_action_map_loader.cpp
  ../asset/caors_action_map_request.cpp ../application/action_map_application.cpp
  ../../asset/action_map_parser.cpp ../../asset/action_map_validation.cpp input_service.cpp)
target_compile_features(gneiss_caors_input PUBLIC cxx_std_20)
target_include_directories(gneiss_caors_input PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_caors_input PUBLIC gneiss_caors_adapter gneiss_core_values
  PRIVATE gneiss_yyjson)
gneiss_target_compile_warnings(gneiss_caors_input)
gneiss_target_output_directories(gneiss_caors_input)
