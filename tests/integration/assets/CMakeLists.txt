# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_asset_application_test application_test.cpp)
set_target_properties(gneiss_asset_application_test PROPERTIES FOLDER "Tests")
target_link_libraries(gneiss_asset_application_test PRIVATE gneiss::engine gneiss_tasks)
gneiss_target_compile_warnings(gneiss_asset_application_test)
gneiss_target_output_directories(gneiss_asset_application_test)
set(input_test_root "${PROJECT_SOURCE_DIR}/examples/temple/assets")
if(EMSCRIPTEN)
  set(input_test_root "/gneiss-input")
endif()
add_test(NAME gneiss.assets.application COMMAND gneiss_asset_application_test "${input_test_root}")
set_tests_properties(gneiss.assets.application PROPERTIES LABELS "cpu;integration")
set_tests_properties(gneiss.assets.application PROPERTIES TIMEOUT 30)
if(WIN32 AND GNEISS_TASK_THREADS)
  target_compile_definitions(gneiss_asset_application_test PRIVATE GNEISS_TEST_NATIVE_WINDOW)
  target_link_libraries(gneiss_asset_application_test PRIVATE user32)
  # 原生窗口关闭需要真实图形设备，与 CPU 加载契约独立验收。
  add_test(NAME gneiss.assets.application-native-window
    COMMAND gneiss_asset_application_test "${input_test_root}" --native-window)
  set_tests_properties(gneiss.assets.application-native-window PROPERTIES TIMEOUT 30 LABELS gpu)
endif()

target_compile_definitions(gneiss_asset_application_test PRIVATE
  GNEISS_TEST_TASK_THREADS=$<BOOL:${GNEISS_TASK_THREADS}>)
if(EMSCRIPTEN)
  target_link_options(gneiss_asset_application_test PRIVATE
    "--embed-file=${PROJECT_SOURCE_DIR}/examples/temple/assets/input@/gneiss-input/input")
endif()

add_executable(gneiss_asset_manager_test asset_manager_test.cpp)
set_target_properties(gneiss_asset_manager_test PROPERTIES FOLDER "Tests")
target_link_libraries(gneiss_asset_manager_test PRIVATE gneiss::engine gneiss_tasks)
target_compile_definitions(gneiss_asset_manager_test PRIVATE GNEISS_TEST_TASK_THREADS=$<BOOL:${GNEISS_TASK_THREADS}>)
gneiss_target_compile_warnings(gneiss_asset_manager_test)
gneiss_target_output_directories(gneiss_asset_manager_test)
add_test(NAME gneiss.assets.manager COMMAND gneiss_asset_manager_test)
set_tests_properties(gneiss.assets.manager PROPERTIES LABELS "cpu;integration")
set_tests_properties(gneiss.assets.manager PROPERTIES TIMEOUT 30)

add_executable(gneiss_file_service_test file_service_test.cpp)
set_target_properties(gneiss_file_service_test PROPERTIES FOLDER "Tests")
target_link_libraries(gneiss_file_service_test PRIVATE gneiss_files)
target_compile_features(gneiss_file_service_test PRIVATE cxx_std_20)
target_compile_definitions(gneiss_file_service_test PRIVATE GNEISS_TEST_TASK_THREADS=$<BOOL:${GNEISS_TASK_THREADS}>)
if(GNEISS_TASK_THREADS AND NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_link_libraries(gneiss_file_service_test PRIVATE Threads::Threads)
endif()
gneiss_target_compile_warnings(gneiss_file_service_test)
gneiss_target_output_directories(gneiss_file_service_test)
add_test(NAME gneiss.files.service COMMAND gneiss_file_service_test)
set_tests_properties(gneiss.files.service PROPERTIES LABELS "cpu;integration")

if(EMSCRIPTEN)
  add_executable(gneiss_browser_file_backend_test browser_file_backend_test.cpp)
  set_target_properties(gneiss_browser_file_backend_test PROPERTIES FOLDER "Tests")
  target_link_libraries(gneiss_browser_file_backend_test PRIVATE gneiss_files)
  target_include_directories(gneiss_browser_file_backend_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
  target_compile_features(gneiss_browser_file_backend_test PRIVATE cxx_std_20)
  target_link_options(gneiss_browser_file_backend_test PRIVATE "-sFETCH=1" "-lidbfs.js" "-sEXIT_RUNTIME=0"
    "SHELL:--js-library \"${PROJECT_SOURCE_DIR}/engine/src/function/files/backends/detail/fetch_stream.js\"")
  set_property(TARGET gneiss_browser_file_backend_test APPEND PROPERTY LINK_DEPENDS
    "${PROJECT_SOURCE_DIR}/engine/src/function/files/backends/detail/fetch_stream.js")
  gneiss_target_compile_warnings(gneiss_browser_file_backend_test)
endif()

if(NOT GNEISS_TASK_THREADS OR EMSCRIPTEN)
  return()
endif()
# 使用正式资源注册入口，验证 CPU 描述与保活契约；不创建 GPU 设备。
add_executable(gneiss_asset_material_test material_description_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/asset_decode.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/resource/mesh/mesh_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/resource/texture/texture_decoder.cpp")
set_target_properties(gneiss_asset_material_test PROPERTIES FOLDER "Tests")
target_compile_definitions(gneiss_asset_material_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_include_directories(gneiss_asset_material_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(gneiss_asset_material_test PRIVATE gneiss::engine gneiss_yyjson gneiss_png granit::granit)

gneiss_target_compile_warnings(gneiss_asset_material_test)
gneiss_target_output_directories(gneiss_asset_material_test)
add_test(NAME gneiss.assets.material COMMAND gneiss_asset_material_test)
set_tests_properties(gneiss.assets.material PROPERTIES LABELS "cpu;integration")
set_tests_properties(gneiss.assets.material PROPERTIES TIMEOUT 30)

if(GNEISS_TASK_THREADS AND NOT EMSCRIPTEN)
  add_executable(gneiss_native_file_backend_test native_file_backend_test.cpp)
  set_target_properties(gneiss_native_file_backend_test PROPERTIES FOLDER "Tests")
  target_include_directories(gneiss_native_file_backend_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
  target_link_libraries(gneiss_native_file_backend_test PRIVATE gneiss_files)
  target_compile_features(gneiss_native_file_backend_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_native_file_backend_test)
  gneiss_target_output_directories(gneiss_native_file_backend_test)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-files")
  add_test(NAME gneiss.files.native COMMAND gneiss_native_file_backend_test "${CMAKE_CURRENT_BINARY_DIR}/native-files")
  set_tests_properties(gneiss.files.native PROPERTIES LABELS "cpu;integration")
  set_tests_properties(gneiss.files.native PROPERTIES TIMEOUT 20)
endif()
