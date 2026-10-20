# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_caors_asset_probe asset_probe.cpp)
target_compile_features(gneiss_caors_asset_probe PRIVATE cxx_std_20)
target_include_directories(gneiss_caors_asset_probe PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_caors_asset_probe PRIVATE gneiss_caors_input gneiss_tasks gneiss_io_uv)
gneiss_target_compile_warnings(gneiss_caors_asset_probe)
gneiss_target_output_directories(gneiss_caors_asset_probe)
add_test(NAME gneiss.caors.asset-probe COMMAND gneiss_caors_asset_probe
  "${PROJECT_SOURCE_DIR}/examples/temple/assets/input/default.input-map.json")
set_tests_properties(gneiss.caors.asset-probe PROPERTIES TIMEOUT 30)

# 资产层桥接只在独立宿主验证；不向平台适配层引入反向依赖。
add_library(gneiss_caors_vfs_bridge STATIC
  "${PROJECT_SOURCE_DIR}/src/engine/asset/caors_vfs_bridge.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/asset/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp")
target_include_directories(gneiss_caors_vfs_bridge PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(gneiss_caors_vfs_bridge PUBLIC gneiss_caors_adapter)
gneiss_target_compile_warnings(gneiss_caors_vfs_bridge)
add_executable(gneiss_caors_vfs_bridge_test vfs_bridge_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/asset/native_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/asset/source_revision_file_system.cpp")
target_compile_definitions(gneiss_caors_vfs_bridge_test PRIVATE GNEISS_ASSET_LIBUV GNEISS_BUILDING_LIBRARY)
target_include_directories(gneiss_caors_vfs_bridge_test PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(gneiss_caors_vfs_bridge_test PRIVATE gneiss_caors_vfs_bridge gneiss_caors_input gneiss_io_uv)
gneiss_target_compile_warnings(gneiss_caors_vfs_bridge_test)
gneiss_target_output_directories(gneiss_caors_vfs_bridge_test)
add_test(NAME gneiss.caors.vfs-bridge COMMAND gneiss_caors_vfs_bridge_test
  "${PROJECT_SOURCE_DIR}/examples/temple/assets")
set_tests_properties(gneiss.caors.vfs-bridge PROPERTIES TIMEOUT 30)
