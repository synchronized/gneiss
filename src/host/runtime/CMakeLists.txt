# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

  add_library(
    gneiss_runtime_ipc STATIC
    ipc/inbound/control_commands.cpp
    ipc/inbound/asset_commands.cpp
    ipc/inbound/scene_commands.cpp
    ipc/inbound/inspection_commands.cpp
    ipc/inbound/property_commands.cpp
    ipc/inbound/session_commands.cpp
    ipc/outbound/heartbeat_response.cpp
    ipc/outbound/asset_events.cpp
    ipc/outbound/inspection_events.cpp
    ipc/outbound/log_events.cpp
    ipc/outbound/property_events.cpp
    ipc/outbound/protocol_error_response.cpp
    ipc/outbound/ready_event.cpp
    ipc/outbound/runtime_ipc_outbound.hpp
    ipc/outbound/session_hello_event.cpp
    ipc/outbound/shutdown_event.cpp
    ipc/outbound/state_event.cpp
    ipc/outbound/statistics_events.cpp
    ipc/runtime_command_context.cpp
    ipc/runtime_command_context.hpp
    ipc/runtime_commands.cpp
    ipc/runtime_commands.hpp
    runtime_ipc_session.cpp
  runtime_ipc_session.hpp
  runtime_asset_reloader.cpp
  runtime_asset_reloader.hpp
  runtime_property_editor.cpp
  runtime_property_editor.hpp
  runtime_scene_inspection.cpp
  runtime_scene_inspection.hpp
  runtime_scene_loader.cpp
  runtime_scene_loader.hpp
)
target_link_libraries(
  gneiss_runtime_ipc PUBLIC gneiss_app_ipc_protocol PRIVATE gneiss_app_project gneiss::engine
)
target_include_directories(gneiss_runtime_ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gneiss_runtime_ipc PUBLIC gneiss_native_interface)
target_compile_features(gneiss_runtime_ipc PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_runtime_ipc)
gneiss_target_output_directories(gneiss_runtime_ipc)

add_executable(gneiss_runtime "${PROJECT_SOURCE_DIR}/apps/runtime/main.cpp" runtime_application.cpp runtime_application.hpp runtime_log.cpp runtime_log.hpp)
target_link_libraries(
  gneiss_runtime
  PRIVATE gneiss_app_project gneiss::engine gneiss_runtime_ipc gneiss_tasks
)
target_link_libraries(gneiss_runtime PRIVATE gneiss_native_interface)
target_compile_features(gneiss_runtime PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_runtime)
gneiss_target_output_directories(gneiss_runtime)

if(BUILD_TESTING)
  add_executable(gneiss_runtime_ipc_session_test tests/runtime_ipc_session_test.cpp)
  target_link_libraries(gneiss_runtime_ipc_session_test PRIVATE gneiss_runtime_ipc)
  target_compile_features(gneiss_runtime_ipc_session_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_runtime_ipc_session_test)
  gneiss_target_output_directories(gneiss_runtime_ipc_session_test)
  add_test(NAME gneiss.runtime.ipc-session COMMAND gneiss_runtime_ipc_session_test)

  add_executable(gneiss_runtime_ipc_command_test tests/runtime_ipc_command_test.cpp)
  target_link_libraries(gneiss_runtime_ipc_command_test PRIVATE gneiss_runtime_ipc)
  target_compile_features(gneiss_runtime_ipc_command_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_runtime_ipc_command_test)
  gneiss_target_output_directories(gneiss_runtime_ipc_command_test)
  add_test(NAME gneiss.runtime.ipc-command COMMAND gneiss_runtime_ipc_command_test)

  add_executable(
    gneiss_runtime_scene_inspection_test tests/runtime_scene_inspection_test.cpp
  )
  target_link_libraries(gneiss_runtime_scene_inspection_test PRIVATE gneiss_runtime_ipc)
  target_include_directories(gneiss_runtime_scene_inspection_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_compile_definitions(
    gneiss_runtime_scene_inspection_test
    PRIVATE GNEISS_RUNTIME_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
  )
  target_compile_features(gneiss_runtime_scene_inspection_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_runtime_scene_inspection_test)
  gneiss_target_output_directories(gneiss_runtime_scene_inspection_test)
  add_test(
    NAME gneiss.runtime.scene-inspection COMMAND gneiss_runtime_scene_inspection_test
  )

  add_executable(gneiss_runtime_property_editor_test tests/runtime_property_editor_test.cpp)
  target_link_libraries(gneiss_runtime_property_editor_test PRIVATE gneiss_runtime_ipc)
  target_compile_features(gneiss_runtime_property_editor_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_runtime_property_editor_test)
  gneiss_target_output_directories(gneiss_runtime_property_editor_test)
  add_test(NAME gneiss.runtime.property-editor COMMAND gneiss_runtime_property_editor_test)

  add_executable(gneiss_runtime_asset_reloader_test tests/runtime_asset_reloader_test.cpp)
  target_link_libraries(gneiss_runtime_asset_reloader_test PRIVATE gneiss_runtime_ipc)
  target_compile_features(gneiss_runtime_asset_reloader_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_runtime_asset_reloader_test)
  gneiss_target_output_directories(gneiss_runtime_asset_reloader_test)
  add_test(NAME gneiss.runtime.asset-reloader COMMAND gneiss_runtime_asset_reloader_test)

  add_executable(gneiss_runtime_log_test tests/runtime_log_test.cpp runtime_log.cpp runtime_log.hpp)
  target_include_directories(gneiss_runtime_log_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(gneiss_runtime_log_test PRIVATE gneiss_app_project gneiss::engine)
  target_compile_features(gneiss_runtime_log_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_runtime_log_test)
  gneiss_target_output_directories(gneiss_runtime_log_test)
  add_test(NAME gneiss.runtime.log-sink COMMAND gneiss_runtime_log_test)


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
  if(GNEISS_BUILD_EXAMPLES)
    add_test(
      NAME gneiss.runtime.installed-smoke
      COMMAND
        "${CMAKE_COMMAND}" -DGNEISS_BUILD_DIR=${PROJECT_BINARY_DIR}
        "-DGNEISS_GRANIT_BUILD_DIR=${GNEISS_GRANIT_FETCHED_BUILD_DIR}"
        -DGNEISS_CONFIG=$<CONFIG> -DGNEISS_SHARED=${BUILD_SHARED_LIBS} -P
        "${PROJECT_SOURCE_DIR}/cmake/check_installed_runtime.cmake"
    )
    set_tests_properties(gneiss.runtime.installed-smoke PROPERTIES TIMEOUT 120)
  endif()
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
    set_tests_properties(gneiss.runtime.stop-protocol PROPERTIES TIMEOUT 50)
  endif()
endif()

if(BUILD_TESTING)

  add_test(NAME gneiss.runtime.async-input-startup
    COMMAND "${CMAKE_COMMAND}"
      -DGNEISS_RUNTIME=$<TARGET_FILE:gneiss_runtime>
      -DSOURCE_PROJECT=${PROJECT_SOURCE_DIR}/examples/editor_demo
      -DINPUT_MAP=${PROJECT_SOURCE_DIR}/examples/temple/assets/input/default.input-map.json
      -DTEST_PARENT=${PROJECT_BINARY_DIR}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/async_input_startup_test.cmake")
  set_tests_properties(gneiss.runtime.async-input-startup PROPERTIES TIMEOUT 60)
endif()

# 异步场景测试由宿主驱动任务，不使用生产同步加载入口。
foreach(test IN ITEMS gneiss_runtime_scene_inspection_test)
  if(TARGET ${test})
    target_link_libraries(${test} PRIVATE gneiss_tasks)
    target_include_directories(${test} PRIVATE "${PROJECT_SOURCE_DIR}/src")
  endif()
endforeach()
