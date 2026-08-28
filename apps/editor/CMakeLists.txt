# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(gneiss_editor_session STATIC editor_session.cpp editor_session.h)
target_link_libraries(gneiss_editor_session PUBLIC gneiss::gneiss)
target_include_directories(gneiss_editor_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_editor_session PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_editor_session)
gneiss_target_output_directories(gneiss_editor_session)

add_executable(gneiss_editor main.cpp imgui_adapter.cpp imgui_adapter.h)
target_link_libraries(gneiss_editor PRIVATE gneiss_editor_session gneiss_imgui)
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
  add_test(
    NAME gneiss.editor.smoke
    COMMAND
      gneiss_editor --smoke --asset-root "${PROJECT_SOURCE_DIR}/tests/data/assets" --scene
      "asset://scenes/triangle.scene.json"
  )
  set_tests_properties(gneiss.editor.smoke PROPERTIES TIMEOUT 30)
endif()
