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
    # Dear ImGui v1.92.9b；MIT 许可。固定提交避免标签移动影响构建复现。
    GIT_TAG f1cc2ae15e53a861a874c3034aae6798fde194ab
    GIT_SHALLOW TRUE
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
