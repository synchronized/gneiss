# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_editor_ui STATIC
  viewport/transform_gizmo_view.cpp
  viewport/transform_gizmo_view.hpp
  panels/author_hierarchy_panel.cpp
  panels/author_hierarchy_panel.hpp
  panels/author_property_panel.cpp
  panels/author_property_panel.hpp
  panels/material_editor_panel.cpp
  panels/material_editor_panel.hpp
  panels/document_action_panel.cpp
  panels/document_action_panel.hpp
  panels/console_panel.cpp
  panels/console_panel.hpp
  panels/runtime_panels.cpp
  panels/runtime_panels.hpp
  editor_ui.cpp
  editor_ui.hpp
  editor_theme.cpp
  editor_theme.hpp
  imgui_adapter.cpp
  imgui_adapter.hpp
)
target_link_libraries(gneiss_editor_ui PUBLIC gneiss::engine gneiss_imgui gneiss_editor_model PRIVATE gneiss_scene_document gneiss_imguizmo)
target_include_directories(gneiss_editor_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gneiss_editor_ui PRIVATE gneiss_native_interface)
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
    PUBLIC gneiss_native_interface
    PUBLIC gneiss::engine gneiss_asset_import_sdk gneiss_tasks
  )
  target_include_directories(gneiss_editor_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(gneiss_editor_assets PUBLIC cxx_std_20)
  gneiss_target_compile_warnings(gneiss_editor_assets)
  gneiss_target_output_directories(gneiss_editor_assets)
  target_sources(gneiss_editor_ui PRIVATE panels/asset_browser_panel.cpp panels/asset_browser_panel.hpp)
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
  commands/author_document_change.hpp
  commands/author_file_save.cpp
  commands/author_file_save.hpp
  commands/prefab_authoring.cpp
  commands/prefab_authoring.hpp
  panels/property_inspector_model.cpp
  panels/property_inspector_model.hpp
  panels/material_editor_model.cpp
  panels/material_editor_model.hpp
  commands/editor_command_history.cpp
  commands/editor_command_history.hpp
  viewport/editor_camera.cpp
  viewport/editor_camera.hpp
  viewport/editor_grid.cpp
  viewport/editor_grid.hpp
  viewport/editor_rotation_math.cpp
  viewport/editor_rotation_math.hpp
  viewport/transform_gizmo_math.cpp
  viewport/transform_gizmo_math.hpp
)
target_link_libraries(gneiss_editor_model PUBLIC gneiss::engine PRIVATE gneiss_yyjson)
target_include_directories(gneiss_editor_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gneiss_editor_model PUBLIC gneiss_native_interface)
target_compile_features(gneiss_editor_model PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_editor_model)
gneiss_target_output_directories(gneiss_editor_model)

# 场景编辑状态与 Gizmo 事务不依赖宿主工程、进程或 IPC。
add_library(
  gneiss_scene_document STATIC
  documents/document_actions.cpp
  documents/document_actions.hpp
  documents/document_editing.cpp
  documents/document_editing.hpp
  documents/editor_scene_loader.cpp
  documents/editor_scene_loader.hpp
  commands/editor_asset_edit.cpp
  commands/editor_asset_edit.hpp
  commands/editor_prefab_edit.cpp
  commands/editor_prefab_edit.hpp
  runtime_launch.cpp
  runtime_launch.hpp
  commands/author_property_edit.cpp
  commands/author_property_edit.hpp
  commands/runtime_author_apply.cpp
  commands/runtime_author_apply.hpp
  documents/scene_document.cpp
  documents/scene_document.hpp
  documents/scene_document_snapshot.cpp
  documents/scene_document_snapshot.hpp
  viewport/transform_gizmo_drag.cpp
  viewport/transform_gizmo_drag.hpp
)
target_link_libraries(gneiss_scene_document PUBLIC gneiss_editor_model gneiss_tasks)
target_include_directories(gneiss_scene_document PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gneiss_scene_document PUBLIC gneiss_native_interface)
target_compile_features(gneiss_scene_document PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_document)
gneiss_target_output_directories(gneiss_scene_document)
