# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)

function(gneiss_resolve_imguizmo)
  if(TARGET gneiss_imguizmo)
    return()
  endif()

  FetchContent_Declare(
    gneiss_imguizmo_source
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    # ImGuizmo；MIT 许可。固定提交保证 Editor 构建可复现。
    GIT_TAG 18cef5e031d8c6973d80284c67f60549fafd78c1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR gneiss-no-cmake
  )
  FetchContent_MakeAvailable(gneiss_imguizmo_source)

  add_library(gneiss_imguizmo STATIC "${gneiss_imguizmo_source_SOURCE_DIR}/src/ImGuizmo.cpp")
  target_include_directories(gneiss_imguizmo SYSTEM PUBLIC
                             "${gneiss_imguizmo_source_SOURCE_DIR}/src")
  target_link_libraries(gneiss_imguizmo PUBLIC gneiss_imgui)
  target_compile_features(gneiss_imguizmo PUBLIC cxx_std_11)
  gneiss_target_output_directories(gneiss_imguizmo)
endfunction()
