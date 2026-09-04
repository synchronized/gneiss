# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_game_module_session STATIC
  game_module_session.cpp
  game_module_session.h
  game_update_scheduler.cpp
  game_update_scheduler.h
)
target_link_libraries(gneiss_game_module_session PUBLIC gneiss::engine gneiss_dynamic_library)
target_include_directories(
  gneiss_game_module_session
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE "${PROJECT_SOURCE_DIR}/src"
)
target_compile_features(gneiss_game_module_session PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_game_module_session)
gneiss_target_output_directories(gneiss_game_module_session)

  add_library(
    gneiss_runtime_ipc STATIC
    ipc/inbound/control_commands.cpp
    ipc/inbound/inspection_commands.cpp
    ipc/inbound/property_commands.cpp
    ipc/inbound/session_commands.cpp
    ipc/runtime_command_context.cpp
    ipc/runtime_command_context.h
    ipc/runtime_commands.cpp
    ipc/runtime_commands.h
    runtime_ipc_session.cpp
  runtime_ipc_session.h
  runtime_property_editor.cpp
  runtime_property_editor.h
  runtime_scene_inspection.cpp
  runtime_scene_inspection.h
)
target_link_libraries(
  gneiss_runtime_ipc PUBLIC gneiss_app_ipc_protocol PRIVATE gneiss_app_project gneiss::engine
)
target_include_directories(gneiss_runtime_ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_runtime_ipc PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_runtime_ipc)
gneiss_target_output_directories(gneiss_runtime_ipc)

add_executable(gneiss_runtime main.cpp runtime_log.cpp runtime_log.h)
target_link_libraries(
  gneiss_runtime
  PRIVATE gneiss_app_project gneiss::engine gneiss_game_module_session gneiss_runtime_ipc
)
target_include_directories(gneiss_runtime PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(gneiss_runtime PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_runtime)
gneiss_target_output_directories(gneiss_runtime)

if(BUILD_TESTING)
  foreach(module_fixture IN ITEMS valid missing_symbol invalid_abi initialize_failure)
    add_library(gneiss_game_module_${module_fixture}_fixture MODULE tests/game_module_fixture.cpp)
    target_link_libraries(gneiss_game_module_${module_fixture}_fixture PRIVATE gneiss::engine)
    target_compile_features(gneiss_game_module_${module_fixture}_fixture PRIVATE cxx_std_20)
    target_compile_definitions(
      gneiss_game_module_${module_fixture}_fixture PRIVATE GNEISS_BUILDING_GAME_MODULE
    )
    gneiss_target_compile_warnings(gneiss_game_module_${module_fixture}_fixture)
    gneiss_target_output_directories(gneiss_game_module_${module_fixture}_fixture)
  endforeach()
  target_compile_definitions(
    gneiss_game_module_missing_symbol_fixture PRIVATE GNEISS_TEST_MISSING_QUERY
  )
  target_compile_definitions(gneiss_game_module_invalid_abi_fixture PRIVATE GNEISS_TEST_INVALID_ABI)
  target_compile_definitions(
    gneiss_game_module_initialize_failure_fixture PRIVATE GNEISS_TEST_INITIALIZE_FAILURE
  )

  add_executable(gneiss_game_module_session_test tests/game_module_session_test.cpp)
  target_link_libraries(gneiss_game_module_session_test PRIVATE gneiss_game_module_session)
  target_compile_features(gneiss_game_module_session_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_game_module_session_test)
  gneiss_target_output_directories(gneiss_game_module_session_test)
  add_test(
    NAME gneiss.runtime.game-module-session
    COMMAND
      gneiss_game_module_session_test $<TARGET_FILE:gneiss_game_module_valid_fixture>
      $<TARGET_FILE:gneiss_game_module_missing_symbol_fixture>
      $<TARGET_FILE:gneiss_game_module_invalid_abi_fixture>
      $<TARGET_FILE:gneiss_game_module_initialize_failure_fixture>
  )

  add_executable(gneiss_game_update_scheduler_test tests/game_update_scheduler_test.cpp)
  target_link_libraries(gneiss_game_update_scheduler_test PRIVATE gneiss_game_module_session)
  target_compile_features(gneiss_game_update_scheduler_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_game_update_scheduler_test)
  gneiss_target_output_directories(gneiss_game_update_scheduler_test)
  add_test(NAME gneiss.runtime.game-update-scheduler COMMAND gneiss_game_update_scheduler_test)

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

  add_executable(gneiss_runtime_log_test tests/runtime_log_test.cpp runtime_log.cpp runtime_log.h)
  target_include_directories(gneiss_runtime_log_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(gneiss_runtime_log_test PRIVATE gneiss_app_project gneiss::engine)
  target_compile_features(gneiss_runtime_log_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_runtime_log_test)
  gneiss_target_output_directories(gneiss_runtime_log_test)
  add_test(NAME gneiss.runtime.log-sink COMMAND gneiss_runtime_log_test)
  add_test(
    NAME gneiss.runtime.project-game-module
    COMMAND
      "${CMAKE_COMMAND}" -DGNEISS_RUNTIME=$<TARGET_FILE:gneiss_runtime>
      -DGAME_MODULE=$<TARGET_FILE:gneiss_game_module_valid_fixture>
      -DSOURCE_PROJECT=${PROJECT_SOURCE_DIR}/examples/editor_demo
      -DTEST_ROOT=${PROJECT_BINARY_DIR}/runtime-game-module-test -P
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_game_module_test.cmake"
  )

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
    set_tests_properties(gneiss.runtime.stop-protocol PROPERTIES TIMEOUT 15)
  endif()
endif()
