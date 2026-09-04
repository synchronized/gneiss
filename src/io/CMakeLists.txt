# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_io_uv STATIC
  uv/uv_loop_access.h
  uv/uv_loop_executor.cpp
  uv/uv_loop_executor.h
  uv/uv_result.cpp
  uv/uv_result.h
)
target_link_libraries(
  gneiss_io_uv
  PUBLIC gneiss::engine
  PRIVATE gneiss::libuv
)
target_include_directories(gneiss_io_uv PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/uv")
target_compile_features(gneiss_io_uv PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_io_uv)
gneiss_target_output_directories(gneiss_io_uv)

if(GNEISS_BUILD_TESTING)
  add_executable(gneiss_uv_loop_executor_test uv/tests/uv_loop_executor_test.cpp)
  target_link_libraries(gneiss_uv_loop_executor_test PRIVATE gneiss_io_uv)
  target_compile_features(gneiss_uv_loop_executor_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_loop_executor_test)
  gneiss_target_output_directories(gneiss_uv_loop_executor_test)
  add_test(NAME gneiss.io.uv-loop-executor COMMAND gneiss_uv_loop_executor_test)

  add_executable(gneiss_uv_loop_executor_stress_test uv/tests/uv_loop_executor_stress_test.cpp)
  target_link_libraries(gneiss_uv_loop_executor_stress_test PRIVATE gneiss_io_uv)
  target_compile_features(gneiss_uv_loop_executor_stress_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_loop_executor_stress_test)
  gneiss_target_output_directories(gneiss_uv_loop_executor_stress_test)
  add_test(NAME gneiss.io.uv-loop-executor-stress COMMAND gneiss_uv_loop_executor_stress_test)

  add_executable(gneiss_uv_result_test uv/tests/uv_result_test.cpp)
  target_link_libraries(gneiss_uv_result_test PRIVATE gneiss_io_uv gneiss::libuv)
  target_compile_features(gneiss_uv_result_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_result_test)
  gneiss_target_output_directories(gneiss_uv_result_test)
  add_test(NAME gneiss.io.uv-result COMMAND gneiss_uv_result_test)

endif()
