# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_runtime main.cpp runtime_log.cpp runtime_log.h)
target_link_libraries(gneiss_runtime PRIVATE gneiss_app_project gneiss::engine)
target_compile_features(gneiss_runtime PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_runtime)
gneiss_target_output_directories(gneiss_runtime)

if(BUILD_TESTING)
  add_test(
    NAME gneiss.runtime.smoke
    COMMAND gneiss_runtime --smoke --project "${PROJECT_SOURCE_DIR}/examples/editor_demo"
  )
  add_test(
    NAME gneiss.runtime.startup-diagnostic
    COMMAND
      "${CMAKE_COMMAND}" -DGNEISS_RUNTIME=$<TARGET_FILE:gneiss_runtime>
      -DMISSING_PROJECT=${PROJECT_BINARY_DIR}/missing-runtime-project
      -DRUNTIME_TEST_DIR=${PROJECT_BINARY_DIR}/runtime-log-test -P
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/startup_diagnostic_test.cmake"
  )
  if(WIN32)
    add_test(
      NAME gneiss.runtime.stop-protocol
      COMMAND
        powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/stop_protocol_test.ps1"
        -Runtime $<TARGET_FILE:gneiss_runtime>
        -Project "${PROJECT_SOURCE_DIR}/examples/editor_demo"
        -TestRoot "${PROJECT_BINARY_DIR}/runtime-stop-protocol-test"
    )
  endif()
  set_tests_properties(gneiss.runtime.smoke PROPERTIES TIMEOUT 30)
  set_tests_properties(gneiss.runtime.startup-diagnostic PROPERTIES TIMEOUT 30)
  if(WIN32)
    set_tests_properties(gneiss.runtime.stop-protocol PROPERTIES TIMEOUT 15)
  endif()
endif()
