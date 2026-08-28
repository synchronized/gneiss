# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_editor_session STATIC
  editor_camera.cpp
  editor_camera.h
  editor_command_history.cpp
  editor_command_history.h
  editor_project.cpp
  editor_project.h
  editor_session.cpp
  editor_session.h
  project_workspace.cpp
  project_workspace.h
  property_inspector_model.cpp
  property_inspector_model.h
)
target_link_libraries(gneiss_editor_session PRIVATE gneiss_yyjson PUBLIC gneiss::gneiss)
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

add_executable(
  gneiss_editor
  main.cpp
  editor_theme.cpp
  editor_theme.h
  imgui_adapter.cpp
  imgui_adapter.h
  native_dialog.cpp
  native_dialog.h
  project_manager.cpp
  project_manager.h
)
target_link_libraries(gneiss_editor PRIVATE gneiss_editor_session gneiss_imgui)
if(TARGET gneiss_editor_assets)
  target_link_libraries(gneiss_editor PRIVATE gneiss_editor_assets)
  target_compile_definitions(gneiss_editor PRIVATE GNEISS_EDITOR_HAS_ASSET_BROWSER=1)
endif()
target_compile_definitions(
  gneiss_editor
  PRIVATE GNEISS_EDITOR_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf"
)
if(WIN32)
  target_link_libraries(gneiss_editor PRIVATE ole32 shell32 uuid)
endif()
gneiss_target_compile_warnings(gneiss_editor)
gneiss_target_output_directories(gneiss_editor)

if(BUILD_TESTING)
  add_executable(gneiss_editor_session_test tests/editor_session_test.cpp)
  target_link_libraries(gneiss_editor_session_test PRIVATE gneiss_editor_session)
  target_compile_definitions(
    gneiss_editor_session_test
    PRIVATE GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
  )
  gneiss_target_compile_warnings(gneiss_editor_session_test)
  gneiss_target_output_directories(gneiss_editor_session_test)
  add_test(NAME gneiss.editor.session COMMAND gneiss_editor_session_test)
  add_executable(gneiss_editor_project_test tests/editor_project_test.cpp)
  target_link_libraries(gneiss_editor_project_test PRIVATE gneiss_editor_session)
  target_compile_definitions(
    gneiss_editor_project_test
    PRIVATE GNEISS_EDITOR_DEMO_PROJECT="${PROJECT_SOURCE_DIR}/examples/editor_demo"
  )
  gneiss_target_compile_warnings(gneiss_editor_project_test)
  gneiss_target_output_directories(gneiss_editor_project_test)
  add_test(NAME gneiss.editor.project COMMAND gneiss_editor_project_test)
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
  set_tests_properties(gneiss.editor.smoke PROPERTIES TIMEOUT 30)
  set_tests_properties(gneiss.editor.project-manager-smoke PROPERTIES TIMEOUT 30)
endif()
