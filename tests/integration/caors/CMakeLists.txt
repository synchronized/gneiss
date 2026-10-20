# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_caors_asset_probe asset_probe.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/function/input/action_map_parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/input/action_map_validation.cpp")
target_compile_features(gneiss_caors_asset_probe PRIVATE cxx_std_20)
target_include_directories(gneiss_caors_asset_probe PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_caors_asset_probe PRIVATE gneiss_caors_adapter gneiss_tasks gneiss_io_uv gneiss_yyjson)
gneiss_target_compile_warnings(gneiss_caors_asset_probe)
gneiss_target_output_directories(gneiss_caors_asset_probe)
add_test(NAME gneiss.caors.asset-probe COMMAND gneiss_caors_asset_probe
  "${PROJECT_SOURCE_DIR}/examples/temple/assets/input/default.input-map.json")
set_tests_properties(gneiss.caors.asset-probe PROPERTIES TIMEOUT 30)
