# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_io_uv STATIC
  uv/uv_async_file.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/io/uv/uv_async_file.hpp"
  uv/uv_path_resolver.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/io/uv/uv_path_resolver.hpp"
  uv/uv_file_reader.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/io/uv/uv_file_reader.hpp"
  uv/uv_loop_access.hpp
  uv/uv_loop_executor.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/io/uv/uv_loop_executor.hpp"
  uv/uv_result.cpp
  uv/uv_result.hpp
)
target_link_libraries(
  gneiss_io_uv
  PUBLIC gneiss_core_values
  PRIVATE gneiss::libuv gneiss_fatal
)
target_include_directories(gneiss_io_uv PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/uv")
target_compile_features(gneiss_io_uv PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_io_uv)
gneiss_target_output_directories(gneiss_io_uv)

if(GNEISS_BUILD_TESTING)
  add_executable(gneiss_uv_async_file_test "${PROJECT_SOURCE_DIR}/tests/platform/io/uv/uv_async_file_test.cpp")
  target_link_libraries(gneiss_uv_async_file_test PRIVATE gneiss_io_uv)
  target_compile_features(gneiss_uv_async_file_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_async_file_test)
  gneiss_target_output_directories(gneiss_uv_async_file_test)
  set_target_properties(gneiss_uv_async_file_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.io.uv-async-file COMMAND gneiss_uv_async_file_test)
  set_tests_properties(gneiss.io.uv-async-file PROPERTIES TIMEOUT 20)

  add_executable(gneiss_uv_path_resolver_test "${PROJECT_SOURCE_DIR}/tests/platform/io/uv/uv_path_resolver_test.cpp")
  target_link_libraries(gneiss_uv_path_resolver_test PRIVATE gneiss_io_uv)
  target_compile_features(gneiss_uv_path_resolver_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_path_resolver_test)
  gneiss_target_output_directories(gneiss_uv_path_resolver_test)
  set_target_properties(gneiss_uv_path_resolver_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.io.uv-path-resolver COMMAND gneiss_uv_path_resolver_test)
  set_tests_properties(gneiss.io.uv-path-resolver PROPERTIES TIMEOUT 20)

  add_executable(gneiss_uv_file_reader_test "${PROJECT_SOURCE_DIR}/tests/platform/io/uv/uv_file_reader_test.cpp")
  target_link_libraries(gneiss_uv_file_reader_test PRIVATE gneiss_io_uv)
  target_compile_features(gneiss_uv_file_reader_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_file_reader_test)
  gneiss_target_output_directories(gneiss_uv_file_reader_test)
  set_target_properties(gneiss_uv_file_reader_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.io.uv-file-reader COMMAND gneiss_uv_file_reader_test)

  add_executable(gneiss_uv_loop_executor_test "${PROJECT_SOURCE_DIR}/tests/platform/io/uv/uv_loop_executor_test.cpp")
  target_link_libraries(gneiss_uv_loop_executor_test PRIVATE gneiss_io_uv gneiss_fatal)
  target_compile_features(gneiss_uv_loop_executor_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_loop_executor_test)
  gneiss_target_output_directories(gneiss_uv_loop_executor_test)
  set_target_properties(gneiss_uv_loop_executor_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.io.uv-loop-executor COMMAND gneiss_uv_loop_executor_test)

  add_executable(gneiss_uv_loop_executor_stress_test "${PROJECT_SOURCE_DIR}/tests/platform/io/uv/uv_loop_executor_stress_test.cpp")
  target_link_libraries(gneiss_uv_loop_executor_stress_test PRIVATE gneiss_io_uv)
  target_compile_features(gneiss_uv_loop_executor_stress_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_loop_executor_stress_test)
  gneiss_target_output_directories(gneiss_uv_loop_executor_stress_test)
  set_target_properties(gneiss_uv_loop_executor_stress_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.io.uv-loop-executor-stress COMMAND gneiss_uv_loop_executor_stress_test)

  add_executable(gneiss_uv_result_test "${PROJECT_SOURCE_DIR}/tests/platform/io/uv/uv_result_test.cpp")
  target_link_libraries(gneiss_uv_result_test PRIVATE gneiss_io_uv gneiss::libuv)
  target_compile_features(gneiss_uv_result_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_result_test)
  gneiss_target_output_directories(gneiss_uv_result_test)
  set_target_properties(gneiss_uv_result_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.io.uv-result COMMAND gneiss_uv_result_test)

endif()

if(GNEISS_BUILD_TESTING)
  foreach(mode IN ITEMS oom unexpected)
    add_test(NAME gneiss.io.uv-callback-fatal-${mode}
      COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gneiss_uv_loop_executor_test>
      -DMODE=${mode} -P ${PROJECT_SOURCE_DIR}/tests/core/check_fatal.cmake)
  endforeach()
  add_test(NAME gneiss.io.uv-status-fatal-oom
    COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gneiss_uv_result_test>
    -DMODE=oom -P ${PROJECT_SOURCE_DIR}/tests/core/check_fatal.cmake)
endif()
