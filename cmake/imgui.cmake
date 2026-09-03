# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)

function(gneiss_resolve_imgui)
  if(TARGET gneiss_imgui)
    return()
  endif()

  FetchContent_Declare(
    gneiss_imgui_source
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    # Dear ImGui Docking 1.93.0 WIP；MIT 许可。固定提交避免浮动分支影响构建复现。
    GIT_TAG fd13a1e8923a0a7077b404fc36fd063b25a0c0b5
    # 锁定提交位于 docking 分支历史中；浅克隆默认分支无法保证包含该对象。
    GIT_SHALLOW FALSE
  )
  FetchContent_MakeAvailable(gneiss_imgui_source)

  add_library(
    gneiss_imgui STATIC
    "${gneiss_imgui_source_SOURCE_DIR}/imgui.cpp"
    "${gneiss_imgui_source_SOURCE_DIR}/imgui_demo.cpp"
    "${gneiss_imgui_source_SOURCE_DIR}/imgui_draw.cpp"
    "${gneiss_imgui_source_SOURCE_DIR}/imgui_tables.cpp"
    "${gneiss_imgui_source_SOURCE_DIR}/imgui_widgets.cpp"
  )
  target_include_directories(gneiss_imgui SYSTEM PUBLIC "${gneiss_imgui_source_SOURCE_DIR}")
  target_compile_features(gneiss_imgui PUBLIC cxx_std_11)
  gneiss_target_output_directories(gneiss_imgui)
endfunction()
