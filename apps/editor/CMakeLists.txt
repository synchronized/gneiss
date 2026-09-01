# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_editor_session STATIC
  console_model.cpp
  console_model.h
  editor_camera.cpp
  editor_camera.h
  editor_command_history.cpp
  editor_command_history.h
  editor_project.h
  editor_rotation_math.cpp
  editor_rotation_math.h
  editor_session.cpp
  editor_session.h
  project_workspace.cpp
  project_workspace.h
  property_inspector_model.cpp
  property_inspector_model.h
  runtime_launch.cpp
  runtime_launch.h
  runtime_process.cpp
  runtime_process.h
  transform_gizmo_math.cpp
  transform_gizmo_math.h
)
target_link_libraries(
  gneiss_editor_session
  PRIVATE gneiss_yyjson gneiss_child_process
  PUBLIC gneiss::engine gneiss_app_project
)
target_include_directories(gneiss_editor_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_editor_session PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_editor_session)
gneiss_target_output_directories(gneiss_editor_session)

if(TARGET gneiss_asset_import_sdk)
  add_library(
    gneiss_editor_assets STATIC
    asset_import_controller.cpp
    asset_import_controller.h
    asset_browser_model.cpp
    asset_browser_model.h
  )
  target_link_libraries(gneiss_editor_assets PUBLIC gneiss_asset_import_sdk)
  target_include_directories(gneiss_editor_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(gneiss_editor_assets PUBLIC cxx_std_20)
  gneiss_target_compile_warnings(gneiss_editor_assets)
  gneiss_target_output_directories(gneiss_editor_assets)
endif()

add_library(
  gneiss_editor_ui STATIC
  editor_ui.cpp
  editor_ui.h
  editor_theme.cpp
  editor_theme.h
  imgui_adapter.cpp
  imgui_adapter.h
)
target_link_libraries(gneiss_editor_ui PUBLIC gneiss::engine gneiss_imgui)
target_include_directories(gneiss_editor_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_editor_ui PUBLIC cxx_std_20)
target_compile_definitions(
  gneiss_editor_ui
  PRIVATE GNEISS_EDITOR_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf"
)
gneiss_target_compile_warnings(gneiss_editor_ui)
gneiss_target_output_directories(gneiss_editor_ui)

add_executable(
  gneiss_editor
  main.cpp
  native_dialog.cpp
  native_dialog.h
  project_manager.cpp
  project_manager.h
)
target_link_libraries(
  gneiss_editor PRIVATE gneiss_editor_session gneiss_editor_ui gneiss_imguizmo
)
if((WIN32 OR UNIX) AND TARGET gneiss_runtime)
  target_compile_definitions(
    gneiss_editor PRIVATE GNEISS_EDITOR_HAS_RUNTIME=1
                          GNEISS_EDITOR_RUNTIME_PATH="$<TARGET_FILE:gneiss_runtime>"
                          GNEISS_EDITOR_CMAKE_PATH="${CMAKE_COMMAND}"
  )
  add_dependencies(gneiss_editor gneiss_runtime)
endif()
if(TARGET gneiss_editor_assets)
  target_link_libraries(gneiss_editor PRIVATE gneiss_editor_assets)
  target_compile_definitions(gneiss_editor PRIVATE GNEISS_EDITOR_HAS_ASSET_BROWSER=1)
endif()
if(WIN32)
  target_link_libraries(gneiss_editor PRIVATE ole32 shell32 uuid)
endif()
gneiss_target_compile_warnings(gneiss_editor)
gneiss_target_output_directories(gneiss_editor)

if(BUILD_TESTING)
  add_executable(gneiss_editor_ui_layout_test tests/editor_ui_layout_test.cpp)
  target_link_libraries(gneiss_editor_ui_layout_test PRIVATE gneiss_editor_ui)
  gneiss_target_compile_warnings(gneiss_editor_ui_layout_test)
  gneiss_target_output_directories(gneiss_editor_ui_layout_test)
  add_test(NAME gneiss.editor.ui-layout COMMAND gneiss_editor_ui_layout_test)
  add_executable(gneiss_editor_session_test tests/editor_session_test.cpp)
  target_link_libraries(gneiss_editor_session_test PRIVATE gneiss_editor_session)
  target_compile_definitions(
    gneiss_editor_session_test
    PRIVATE GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
  )
  gneiss_target_compile_warnings(gneiss_editor_session_test)
  gneiss_target_output_directories(gneiss_editor_session_test)
  add_test(NAME gneiss.editor.session COMMAND gneiss_editor_session_test)
  add_executable(gneiss_editor_project_workspace_test tests/project_workspace_test.cpp)
  target_link_libraries(gneiss_editor_project_workspace_test PRIVATE gneiss_editor_session)
  gneiss_target_compile_warnings(gneiss_editor_project_workspace_test)
  gneiss_target_output_directories(gneiss_editor_project_workspace_test)
  add_test(NAME gneiss.editor.project-workspace COMMAND gneiss_editor_project_workspace_test)
  add_executable(gneiss_editor_save_test tests/editor_save_test.cpp)
  target_link_libraries(gneiss_editor_save_test PRIVATE gneiss_editor_session)
  target_compile_definitions(
    gneiss_editor_save_test
    PRIVATE GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
  )
  gneiss_target_compile_warnings(gneiss_editor_save_test)
  gneiss_target_output_directories(gneiss_editor_save_test)
  add_test(NAME gneiss.editor.save COMMAND gneiss_editor_save_test)
  add_executable(gneiss_editor_camera_test tests/editor_camera_test.cpp)
  target_link_libraries(gneiss_editor_camera_test PRIVATE gneiss_editor_session)
  gneiss_target_compile_warnings(gneiss_editor_camera_test)
  gneiss_target_output_directories(gneiss_editor_camera_test)
  add_test(NAME gneiss.editor.camera COMMAND gneiss_editor_camera_test)
  add_executable(gneiss_editor_property_inspector_test tests/property_inspector_model_test.cpp)
  target_link_libraries(gneiss_editor_property_inspector_test PRIVATE gneiss_editor_session)
  gneiss_target_compile_warnings(gneiss_editor_property_inspector_test)
  gneiss_target_output_directories(gneiss_editor_property_inspector_test)
  add_test(NAME gneiss.editor.property-inspector COMMAND gneiss_editor_property_inspector_test)
  add_executable(gneiss_editor_command_history_test tests/editor_command_history_test.cpp)
  target_link_libraries(gneiss_editor_command_history_test PRIVATE gneiss_editor_session)
  gneiss_target_compile_warnings(gneiss_editor_command_history_test)
  gneiss_target_output_directories(gneiss_editor_command_history_test)
  add_test(NAME gneiss.editor.command-history COMMAND gneiss_editor_command_history_test)
  add_executable(gneiss_editor_transform_gizmo_math_test tests/transform_gizmo_math_test.cpp)
  target_link_libraries(gneiss_editor_transform_gizmo_math_test PRIVATE gneiss_editor_session)
  gneiss_target_compile_warnings(gneiss_editor_transform_gizmo_math_test)
  gneiss_target_output_directories(gneiss_editor_transform_gizmo_math_test)
  add_test(NAME gneiss.editor.transform-gizmo-math COMMAND gneiss_editor_transform_gizmo_math_test)
  add_executable(gneiss_editor_rotation_math_test tests/editor_rotation_math_test.cpp)
  target_link_libraries(gneiss_editor_rotation_math_test PRIVATE gneiss_editor_session)
  gneiss_target_compile_warnings(gneiss_editor_rotation_math_test)
  gneiss_target_output_directories(gneiss_editor_rotation_math_test)
  add_test(NAME gneiss.editor.rotation-math COMMAND gneiss_editor_rotation_math_test)
  add_executable(gneiss_editor_runtime_launch_test tests/runtime_launch_test.cpp)
  target_link_libraries(gneiss_editor_runtime_launch_test PRIVATE gneiss_editor_session)
  gneiss_target_compile_warnings(gneiss_editor_runtime_launch_test)
  gneiss_target_output_directories(gneiss_editor_runtime_launch_test)
  add_test(NAME gneiss.editor.runtime-launch COMMAND gneiss_editor_runtime_launch_test)
  add_executable(gneiss_editor_console_model_test tests/console_model_test.cpp)
  target_link_libraries(gneiss_editor_console_model_test PRIVATE gneiss_editor_session)
  gneiss_target_compile_warnings(gneiss_editor_console_model_test)
  gneiss_target_output_directories(gneiss_editor_console_model_test)
  add_test(NAME gneiss.editor.console-model COMMAND gneiss_editor_console_model_test)
  if((WIN32 OR UNIX) AND TARGET gneiss_runtime)
    add_executable(gneiss_child_process_fixture tests/child_process_fixture.cpp)
    target_compile_features(gneiss_child_process_fixture PRIVATE cxx_std_20)
    gneiss_target_compile_warnings(gneiss_child_process_fixture)
    gneiss_target_output_directories(gneiss_child_process_fixture)
    add_executable(gneiss_child_process_test tests/child_process_test.cpp)
    target_link_libraries(gneiss_child_process_test PRIVATE gneiss_child_process)
    target_compile_definitions(
      gneiss_child_process_test
      PRIVATE
        GNEISS_TEST_CHILD_PROCESS="$<TARGET_FILE:gneiss_child_process_fixture>"
    )
    gneiss_target_compile_warnings(gneiss_child_process_test)
    gneiss_target_output_directories(gneiss_child_process_test)
    add_dependencies(gneiss_child_process_test gneiss_child_process_fixture)
    add_test(NAME gneiss.process.child-process COMMAND gneiss_child_process_test)
    set_tests_properties(gneiss.process.child-process PROPERTIES TIMEOUT 15)
    add_executable(gneiss_editor_runtime_process_test tests/runtime_process_test.cpp)
    target_link_libraries(gneiss_editor_runtime_process_test PRIVATE gneiss_editor_session)
    target_compile_definitions(
      gneiss_editor_runtime_process_test
      PRIVATE
        GNEISS_TEST_RUNTIME="$<TARGET_FILE:gneiss_runtime>"
        GNEISS_TEST_CHILD_PROCESS="$<TARGET_FILE:gneiss_child_process_fixture>"
        GNEISS_TEST_PROJECT_ROOT="${PROJECT_SOURCE_DIR}/examples/editor_demo"
    )
    gneiss_target_compile_warnings(gneiss_editor_runtime_process_test)
    gneiss_target_output_directories(gneiss_editor_runtime_process_test)
    add_dependencies(
      gneiss_editor_runtime_process_test gneiss_runtime gneiss_child_process_fixture
    )
    add_test(NAME gneiss.editor.runtime-process COMMAND gneiss_editor_runtime_process_test)
    set_tests_properties(gneiss.editor.runtime-process PROPERTIES TIMEOUT 15)
  endif()
  if(TARGET gneiss_editor_assets)
    add_executable(gneiss_editor_asset_browser_test tests/asset_browser_model_test.cpp)
    target_link_libraries(gneiss_editor_asset_browser_test PRIVATE gneiss_editor_assets)
    gneiss_target_compile_warnings(gneiss_editor_asset_browser_test)
    gneiss_target_output_directories(gneiss_editor_asset_browser_test)
    add_test(NAME gneiss.editor.asset-browser COMMAND gneiss_editor_asset_browser_test)
    add_executable(gneiss_editor_asset_import_test tests/asset_import_controller_test.cpp)
    target_link_libraries(gneiss_editor_asset_import_test PRIVATE gneiss_editor_assets)
    target_compile_definitions(
      gneiss_editor_asset_import_test
      PRIVATE GNEISS_EDITOR_TEST_GLTF_ROOT="${PROJECT_SOURCE_DIR}/tests/data/gltf"
    )
    gneiss_target_compile_warnings(gneiss_editor_asset_import_test)
    gneiss_target_output_directories(gneiss_editor_asset_import_test)
    add_test(NAME gneiss.editor.asset-import COMMAND gneiss_editor_asset_import_test)
  endif()
  add_test(
    NAME gneiss.editor.smoke
    COMMAND
      gneiss_editor --smoke --project "${PROJECT_SOURCE_DIR}/examples/editor_demo"
  )
  add_test(NAME gneiss.editor.project-manager-smoke COMMAND gneiss_editor --smoke)
  add_test(
    NAME gneiss.editor.startup-diagnostic
    COMMAND
      "${CMAKE_COMMAND}" -DGNEISS_EDITOR=$<TARGET_FILE:gneiss_editor>
      -DMISSING_PROJECT=${PROJECT_BINARY_DIR}/missing-editor-project -P
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/startup_diagnostic_test.cmake"
  )
  set_tests_properties(gneiss.editor.smoke PROPERTIES TIMEOUT 30)
  set_tests_properties(gneiss.editor.project-manager-smoke PROPERTIES TIMEOUT 30)
  set_tests_properties(gneiss.editor.startup-diagnostic PROPERTIES TIMEOUT 30)
endif()
