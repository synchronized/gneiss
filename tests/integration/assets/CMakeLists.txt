# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_asset_application_test application_test.cpp)
target_include_directories(gneiss_asset_application_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_asset_application_test PRIVATE gneiss::engine gneiss_tasks)
gneiss_target_compile_warnings(gneiss_asset_application_test)
gneiss_target_output_directories(gneiss_asset_application_test)
set(input_test_root "${PROJECT_SOURCE_DIR}/examples/temple/assets")
if(EMSCRIPTEN)
  set(input_test_root "/gneiss-input")
endif()
add_test(NAME gneiss.assets.application COMMAND gneiss_asset_application_test "${input_test_root}")
set_tests_properties(gneiss.assets.application PROPERTIES TIMEOUT 30)
if(GNEISS_ENABLE_GRANIT_PLATFORM AND WIN32 AND GNEISS_TASK_THREADS)
  target_compile_definitions(gneiss_asset_application_test PRIVATE GNEISS_TEST_NATIVE_WINDOW)
  target_link_libraries(gneiss_asset_application_test PRIVATE user32)
endif()

target_compile_definitions(gneiss_asset_application_test PRIVATE
  GNEISS_TEST_TASK_THREADS=$<BOOL:${GNEISS_TASK_THREADS}>)
if(EMSCRIPTEN)
  target_link_options(gneiss_asset_application_test PRIVATE
    "--embed-file=${PROJECT_SOURCE_DIR}/examples/temple/assets/input@/gneiss-input/input")
endif()

add_executable(gneiss_asset_manager_test asset_manager_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/vfs/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp")
target_include_directories(gneiss_asset_manager_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_asset_manager_test PRIVATE gneiss::engine gneiss_tasks)
gneiss_target_compile_warnings(gneiss_asset_manager_test)
gneiss_target_output_directories(gneiss_asset_manager_test)
add_test(NAME gneiss.assets.manager COMMAND gneiss_asset_manager_test)
set_tests_properties(gneiss.assets.manager PROPERTIES TIMEOUT 30)

add_executable(gneiss_asset_source_test asset_source_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/vfs/asset_source.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/vfs/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/vfs/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp")
target_compile_definitions(gneiss_asset_source_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_include_directories(gneiss_asset_source_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_asset_source_test PRIVATE gneiss_core_values)
gneiss_target_compile_warnings(gneiss_asset_source_test)
gneiss_target_output_directories(gneiss_asset_source_test)
add_test(NAME gneiss.assets.source COMMAND gneiss_asset_source_test)

add_executable(gneiss_file_service_test file_service_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/files/file_service.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/files/memory_file_backend.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/files/async_read_file_backend.cpp")
target_include_directories(gneiss_file_service_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_file_service_test PRIVATE gneiss_core_values)
target_compile_features(gneiss_file_service_test PRIVATE cxx_std_20)
target_compile_definitions(gneiss_file_service_test PRIVATE GNEISS_TEST_TASK_THREADS=$<BOOL:${GNEISS_TASK_THREADS}>)
if(GNEISS_TASK_THREADS AND NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_link_libraries(gneiss_file_service_test PRIVATE Threads::Threads)
endif()
gneiss_target_compile_warnings(gneiss_file_service_test)
gneiss_target_output_directories(gneiss_file_service_test)
add_test(NAME gneiss.files.service COMMAND gneiss_file_service_test)

if(NOT GNEISS_TASK_THREADS OR EMSCRIPTEN)
  return()
endif()
# 材质文档验证只链接 CPU 准备实现，不链接 Engine 或创建渲染资源。
add_executable(gneiss_asset_material_test material_description_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/core/guid.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/vfs/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/vfs/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/vfs/native_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/loaders/asset_preparation.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/loaders/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/loaders/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/loaders/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/loaders/texture_container.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/loaders/png_decoder.cpp")
target_compile_definitions(gneiss_asset_material_test PRIVATE GNEISS_ASSET_LIBUV GNEISS_BUILDING_LIBRARY)
target_include_directories(gneiss_asset_material_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_asset_material_test PRIVATE
  gneiss_tasks gneiss_io_uv gneiss_core_values gneiss_yyjson gneiss_png granit::granit)
gneiss_target_compile_warnings(gneiss_asset_material_test)
gneiss_target_output_directories(gneiss_asset_material_test)
add_test(NAME gneiss.assets.material COMMAND gneiss_asset_material_test)
set_tests_properties(gneiss.assets.material PROPERTIES TIMEOUT 30)
