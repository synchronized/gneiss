# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_caors_application_test application_test.cpp)
target_include_directories(gneiss_caors_application_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_caors_application_test PRIVATE gneiss::engine gneiss_tasks)
gneiss_target_compile_warnings(gneiss_caors_application_test)
gneiss_target_output_directories(gneiss_caors_application_test)
set(input_test_root "${PROJECT_SOURCE_DIR}/examples/temple/assets")
if(EMSCRIPTEN)
  set(input_test_root "/gneiss-input")
endif()
add_test(NAME gneiss.caors.application COMMAND gneiss_caors_application_test "${input_test_root}")
set_tests_properties(gneiss.caors.application PROPERTIES TIMEOUT 30)
if(GNEISS_ENABLE_GRANIT_PLATFORM AND WIN32 AND GNEISS_TASK_THREADS)
  target_compile_definitions(gneiss_caors_application_test PRIVATE GNEISS_TEST_NATIVE_WINDOW)
  target_link_libraries(gneiss_caors_application_test PRIVATE user32)
endif()

target_compile_definitions(gneiss_caors_application_test PRIVATE
  GNEISS_TEST_TASK_THREADS=$<BOOL:${GNEISS_TASK_THREADS}>)
if(EMSCRIPTEN)
  target_link_options(gneiss_caors_application_test PRIVATE
    "--embed-file=${PROJECT_SOURCE_DIR}/examples/temple/assets/input@/gneiss-input/input")
endif()

# 直接 libuv I/O 和 Granit 材质探针只适用于原生线程平台。
if(NOT GNEISS_TASK_THREADS OR EMSCRIPTEN)
  return()
endif()

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
  "${PROJECT_SOURCE_DIR}/src/engine/resource/caors_vfs_bridge.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp")
target_include_directories(gneiss_caors_vfs_bridge PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(gneiss_caors_vfs_bridge PUBLIC gneiss_caors_adapter)
gneiss_target_compile_warnings(gneiss_caors_vfs_bridge)
add_executable(gneiss_caors_vfs_bridge_test vfs_bridge_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/native_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/source_revision_file_system.cpp")
target_compile_definitions(gneiss_caors_vfs_bridge_test PRIVATE GNEISS_ASSET_LIBUV GNEISS_BUILDING_LIBRARY)
target_include_directories(gneiss_caors_vfs_bridge_test PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(gneiss_caors_vfs_bridge_test PRIVATE gneiss_caors_vfs_bridge gneiss_caors_input gneiss_io_uv)
gneiss_target_compile_warnings(gneiss_caors_vfs_bridge_test)
gneiss_target_output_directories(gneiss_caors_vfs_bridge_test)
add_test(NAME gneiss.caors.vfs-bridge COMMAND gneiss_caors_vfs_bridge_test
  "${PROJECT_SOURCE_DIR}/examples/temple/assets")
set_tests_properties(gneiss.caors.vfs-bridge PROPERTIES TIMEOUT 30)

# 材质文档验证只链接 CPU 准备实现，不链接 Engine 或创建渲染资源。
add_executable(gneiss_caors_material_document_test material_document_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/native_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_preparation.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/cpu_document_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture_container.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/source_revision_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/png_decoder.cpp")
target_compile_definitions(gneiss_caors_material_document_test PRIVATE GNEISS_ASSET_LIBUV GNEISS_BUILDING_LIBRARY)
target_include_directories(gneiss_caors_material_document_test PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(gneiss_caors_material_document_test PRIVATE
  gneiss_caors_vfs_bridge gneiss_tasks gneiss_io_uv gneiss_core_values gneiss_yyjson gneiss_png granit::granit)
gneiss_target_compile_warnings(gneiss_caors_material_document_test)
gneiss_target_output_directories(gneiss_caors_material_document_test)
add_test(NAME gneiss.caors.material-document COMMAND gneiss_caors_material_document_test)
set_tests_properties(gneiss.caors.material-document PROPERTIES TIMEOUT 30)
