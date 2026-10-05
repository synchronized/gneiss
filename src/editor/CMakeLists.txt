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
