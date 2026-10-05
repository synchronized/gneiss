# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_editor_ui STATIC
  editor_ui.cpp
  editor_ui.hpp
  editor_theme.cpp
  editor_theme.hpp
  imgui_adapter.cpp
  imgui_adapter.hpp
)
target_link_libraries(gneiss_editor_ui PUBLIC gneiss::engine gneiss_imgui)
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
endif()
