# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_application_log_test application_log_test.cpp)
target_link_libraries(gneiss_application_log_test PRIVATE gneiss::engine)
target_compile_features(gneiss_application_log_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_application_log_test)
gneiss_target_output_directories(gneiss_application_log_test)
set_target_properties(gneiss_application_log_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.application_log COMMAND gneiss_application_log_test)
set_tests_properties(gneiss.application_log PROPERTIES LABELS "cpu;integration")

add_executable(gneiss_native_application_flow_test native_application_flow_test.cpp)
target_link_libraries(gneiss_native_application_flow_test PRIVATE gneiss::engine gneiss_tasks)
target_compile_features(gneiss_native_application_flow_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_native_application_flow_test)
target_compile_definitions(
  gneiss_native_application_flow_test PRIVATE GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
)
gneiss_target_output_directories(gneiss_native_application_flow_test)
set_target_properties(gneiss_native_application_flow_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.native-application-flow COMMAND gneiss_native_application_flow_test)
set_tests_properties(gneiss.native-application-flow PROPERTIES LABELS "cpu;integration")

# 原生模块接口不依赖 Engine 的平台私有布局开关。
if(GNEISS_TASK_THREADS AND NOT EMSCRIPTEN)
  add_executable(gneiss_native_application_test native_application_test.cpp)
    target_compile_definitions(gneiss_native_application_test PRIVATE
    GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets")
  target_link_libraries(gneiss_native_application_test PRIVATE gneiss::engine gneiss_tasks Threads::Threads)
  gneiss_target_compile_warnings(gneiss_native_application_test)
  gneiss_target_output_directories(gneiss_native_application_test)
  set_target_properties(gneiss_native_application_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.native-application COMMAND gneiss_native_application_test)
  set_tests_properties(gneiss.native-application PROPERTIES LABELS "cpu;integration")
endif()
add_executable(gneiss_loop_timing_test loop_timing_test.cpp)
target_link_libraries(gneiss_loop_timing_test PRIVATE Threads::Threads)
target_link_libraries(gneiss_loop_timing_test PRIVATE gneiss_core)
target_compile_features(gneiss_loop_timing_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_loop_timing_test)
gneiss_target_output_directories(gneiss_loop_timing_test)
set_target_properties(gneiss_loop_timing_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.loop_timing COMMAND gneiss_loop_timing_test)
set_tests_properties(gneiss.loop_timing PROPERTIES LABELS "cpu;unit")

if(NOT EMSCRIPTEN AND GNEISS_TASK_THREADS)
  add_executable(gneiss_main_loop_wakeup_test main_loop_wakeup_test.cpp)
  target_include_directories(gneiss_main_loop_wakeup_test PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/src")
  target_link_libraries(gneiss_main_loop_wakeup_test PRIVATE gneiss_tasks Threads::Threads)
  gneiss_target_compile_warnings(gneiss_main_loop_wakeup_test)
  gneiss_target_output_directories(gneiss_main_loop_wakeup_test)
  set_target_properties(gneiss_main_loop_wakeup_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.main-loop-wakeup COMMAND gneiss_main_loop_wakeup_test)
  set_tests_properties(gneiss.main-loop-wakeup PROPERTIES LABELS "cpu;integration")
  set_tests_properties(gneiss.main-loop-wakeup PROPERTIES TIMEOUT 30)
endif()
