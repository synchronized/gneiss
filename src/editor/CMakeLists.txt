# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_editor_ui STATIC
  author_hierarchy_panel.cpp
  author_hierarchy_panel.hpp
  author_property_panel.cpp
  author_property_panel.hpp
  console_panel.cpp
  console_panel.hpp
  runtime_panels.cpp
  runtime_panels.hpp
  editor_ui.cpp
  editor_ui.hpp
  editor_theme.cpp
  editor_theme.hpp
  imgui_adapter.cpp
  imgui_adapter.hpp
)
target_link_libraries(gneiss_editor_ui PUBLIC gneiss::engine gneiss_imgui PRIVATE gneiss_editor_model)
target_include_directories(gneiss_editor_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_editor_ui PUBLIC cxx_std_20)
target_compile_definitions(
  gneiss_editor_ui
  PRIVATE GNEISS_EDITOR_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf"
          GNEISS_EDITOR_CJK_FONT_PATH="${GNEISS_EDITOR_CJK_FONT_PATH}"
)
gneiss_target_compile_warnings(gneiss_editor_ui)
gneiss_target_output_directories(gneiss_editor_ui)

if(TARGET gneiss_asset_import_sdk)
  add_library(
    gneiss_editor_assets STATIC
    author_asset_service.cpp
    author_asset_service.hpp
    author_asset_monitor.cpp
    author_asset_monitor.hpp
    asset_file_watcher.cpp
    asset_file_watcher.hpp
    asset_import_controller.cpp
    asset_import_controller.hpp
    asset_reimport_queue.cpp
    asset_reimport_queue.hpp
    asset_background_worker.cpp
    asset_background_worker.hpp
    asset_browser_model.cpp
    asset_browser_model.hpp
  )
  target_link_libraries(
    gneiss_editor_assets
    PRIVATE gneiss_io_uv gneiss::libuv
    PUBLIC gneiss::engine gneiss_asset_import_sdk gneiss_tasks
  )
  target_include_directories(gneiss_editor_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(gneiss_editor_assets PUBLIC cxx_std_20)
  gneiss_target_compile_warnings(gneiss_editor_assets)
  gneiss_target_output_directories(gneiss_editor_assets)
  target_sources(gneiss_editor_ui PRIVATE asset_browser_panel.cpp asset_browser_panel.hpp)
  target_link_libraries(gneiss_editor_ui PRIVATE gneiss_editor_assets)
endif()

# 不依赖宿主工程与进程协议的编辑模型。
add_library(
  gneiss_editor_model STATIC
  runtime_object_id.hpp
  runtime_scene_data.hpp
  runtime_scene_mirror.cpp
  runtime_scene_mirror.hpp
  runtime_property_edits.cpp
  runtime_property_edits.hpp
  console_model.cpp
  console_model.hpp
  author_transaction.cpp
  author_transaction.hpp
  native_author_transaction.cpp
  native_author_transaction.hpp
  prefab_authoring.cpp
  prefab_authoring.hpp
  property_inspector_model.cpp
  property_inspector_model.hpp
  editor_command_history.cpp
  editor_command_history.hpp
  editor_camera.cpp
  editor_camera.hpp
  editor_grid.cpp
  editor_grid.hpp
  editor_rotation_math.cpp
  editor_rotation_math.hpp
  transform_gizmo_math.cpp
  transform_gizmo_math.hpp
)
target_link_libraries(gneiss_editor_model PUBLIC gneiss::engine PRIVATE gneiss_yyjson)
target_include_directories(gneiss_editor_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_editor_model PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_editor_model)
gneiss_target_output_directories(gneiss_editor_model)

# 场景编辑状态与 Gizmo 事务不依赖宿主工程、进程或 IPC。
add_library(
  gneiss_editor_session STATIC
  runtime_launch.cpp
  runtime_launch.hpp
  asset_scene_commands.cpp
  asset_scene_commands.hpp
  author_property_edit.cpp
  author_property_edit.hpp
  runtime_author_apply.cpp
  runtime_author_apply.hpp
  editor_session.cpp
  editor_session.hpp
  transform_gizmo_drag.cpp
  transform_gizmo_drag.hpp
)
target_link_libraries(gneiss_editor_session PUBLIC gneiss_editor_model)
target_include_directories(gneiss_editor_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_editor_session PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_editor_session)
gneiss_target_output_directories(gneiss_editor_session)
