# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_editor main.cpp imgui_adapter.cpp imgui_adapter.h)
target_link_libraries(gneiss_editor PRIVATE gneiss::gneiss gneiss_imgui)
gneiss_target_compile_warnings(gneiss_editor)
gneiss_target_output_directories(gneiss_editor)

if(BUILD_TESTING)
  add_test(NAME gneiss.editor.smoke COMMAND gneiss_editor --smoke)
  set_tests_properties(gneiss.editor.smoke PROPERTIES TIMEOUT 30)
endif()
