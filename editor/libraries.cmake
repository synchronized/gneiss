# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_editor_ui STATIC
  viewport/scene_viewport.cpp
  viewport/scene_viewport.hpp
  ui/viewport/scene_picking.cpp
  ui/viewport/scene_picking.hpp
  ui/viewport/transform_gizmo_drag.cpp
  ui/viewport/transform_gizmo_drag.hpp
  ui/viewport/transform_gizmo_view.cpp
  ui/viewport/transform_gizmo_view.hpp
  ui/panels/author_hierarchy_panel.cpp
  ui/panels/author_hierarchy_panel.hpp
  ui/panels/reflected_property_editor.cpp
  ui/panels/reflected_property_editor.hpp
  ui/panels/property_path.hpp
  ui/panels/material_editor_panel.cpp
  ui/panels/material_editor_panel.hpp
  ui/panels/document_action_panel.cpp
  ui/panels/document_action_panel.hpp
  ui/panels/console_panel.cpp
  ui/panels/console_panel.hpp
  ui/panels/runtime_panels.cpp
  ui/panels/runtime_panels.hpp
  ui/editor_ui.cpp
  ui/editor_ui.hpp
  ui/editor_theme.cpp
  ui/editor_theme.hpp
  ui/imgui_adapter.cpp
  ui/imgui_adapter.hpp
)
target_link_libraries(gneiss_editor_ui PUBLIC gneiss::engine gneiss_imgui gneiss_editor_model PRIVATE gneiss_scene_document gneiss_imguizmo)
target_include_directories(gneiss_editor_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_editor_ui PUBLIC cxx_std_20)
target_compile_definitions(
  gneiss_editor_ui
  PRIVATE GNEISS_EDITOR_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter-Regular.ttf"
          GNEISS_EDITOR_CJK_FONT_PATH="${GNEISS_EDITOR_CJK_FONT_PATH}"
)
gneiss_target_compile_warnings(gneiss_editor_ui)
gneiss_target_output_directories(gneiss_editor_ui)

if(TARGET gneiss_tooling)
  add_library(
    gneiss_editor_assets STATIC
    assets/author_asset_service.cpp
    assets/author_asset_service.hpp
    assets/author_asset_monitor.cpp
    assets/author_asset_monitor.hpp
    assets/asset_file_watcher.cpp
    assets/asset_file_watcher.hpp
    assets/asset_import_controller.cpp
    assets/asset_import_controller.hpp
    assets/asset_reimport_queue.cpp
    assets/asset_reimport_queue.hpp
    assets/asset_background_worker.cpp
    assets/asset_background_worker.hpp
    assets/asset_browser_model.cpp
    assets/asset_browser_model.hpp
  )
  target_link_libraries(
    gneiss_editor_assets
    PRIVATE gneiss_io gneiss::libuv
    PUBLIC gneiss::engine gneiss_tooling gneiss_tasks
  )
  target_include_directories(gneiss_editor_assets
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${PROJECT_SOURCE_DIR}/engine/src"
  )
  target_compile_features(gneiss_editor_assets PUBLIC cxx_std_20)
  gneiss_target_compile_warnings(gneiss_editor_assets)
  gneiss_target_output_directories(gneiss_editor_assets)
  target_sources(gneiss_editor_ui PRIVATE ui/panels/asset_browser_panel.cpp ui/panels/asset_browser_panel.hpp)
  target_link_libraries(gneiss_editor_ui PRIVATE gneiss_editor_assets)
endif()

# 不依赖宿主工程与进程协议的编辑模型。
add_library(
  gneiss_editor_model STATIC
  documents/components/component_registration.cpp
  documents/components/component_registration.hpp
  inspection/runtime_object_id.hpp
  inspection/runtime_scene_data.hpp
  inspection/runtime_scene_mirror.cpp
  inspection/runtime_scene_mirror.hpp
  inspection/runtime_property_edits.cpp
  inspection/runtime_property_edits.hpp
  inspection/console_model.cpp
  inspection/console_model.hpp
  documents/commands/author_document_change.hpp
  documents/commands/author_file_save.cpp
  documents/commands/author_file_save.hpp
  documents/commands/prefab_authoring.cpp
  documents/commands/prefab_authoring.hpp
  ui/panels/material_editor_model.cpp
  ui/panels/material_editor_model.hpp
  documents/commands/editor_command_history.cpp
  documents/commands/editor_command_history.hpp
  ui/viewport/editor_camera.cpp
  ui/viewport/editor_camera.hpp
  ui/viewport/editor_grid.cpp
  ui/viewport/editor_grid.hpp
  ui/viewport/editor_rotation_math.cpp
  ui/viewport/editor_rotation_math.hpp
  ui/viewport/transform_gizmo_math.cpp
  ui/viewport/transform_gizmo_math.hpp
)
target_link_libraries(gneiss_editor_model PUBLIC gneiss::engine PRIVATE gneiss_yyjson)
target_include_directories(gneiss_editor_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_editor_model PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_editor_model)
gneiss_target_output_directories(gneiss_editor_model)

# 场景文档、投影与编辑操作；视口交互在 gneiss_editor_ui 中复用。
add_library(
  gneiss_scene_document STATIC
  documents/document_actions.cpp
  documents/document_actions.hpp
  documents/document_editing.cpp
  documents/document_editing.hpp
  documents/editor_scene_loader.cpp
  documents/editor_scene_loader.hpp
  documents/commands/editor_asset_edit.cpp
  documents/commands/editor_asset_edit.hpp
  documents/commands/editor_prefab_edit.cpp
  documents/commands/editor_prefab_edit.hpp
  runtime/runtime_launch.cpp
  runtime/runtime_launch.hpp
  documents/commands/author_property_edit.cpp
  documents/commands/author_property_edit.hpp
  documents/commands/runtime_author_apply.cpp
  documents/commands/runtime_author_apply.hpp
  documents/document_patch.cpp
  documents/document_patch.hpp
  documents/scene_projection.cpp
  documents/scene_projection.hpp
  documents/prefab_edit.cpp
  documents/prefab_edit.hpp
  documents/scene_edit_session.cpp
  documents/scene_edit_session.hpp
  documents/scene_document_edit.cpp
  documents/scene_document_edit.hpp
  documents/scene_document_snapshot.cpp
  documents/scene_document_snapshot.hpp
  documents/scene_document.cpp
  documents/scene_document.hpp
)
target_link_libraries(gneiss_scene_document PUBLIC gneiss_editor_model gneiss_tasks)
target_include_directories(gneiss_scene_document PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_scene_document PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_document)
gneiss_target_output_directories(gneiss_scene_document)

target_link_libraries(gneiss_editor_assets PRIVATE gneiss_base)

target_link_libraries(gneiss_editor_model PUBLIC gneiss_events)
