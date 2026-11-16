# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(
  gneiss_app_project_description_test
  "${CMAKE_CURRENT_SOURCE_DIR}/project_description_test.cpp"
)
target_link_libraries(gneiss_app_project_description_test PRIVATE gneiss_project_config)
target_compile_definitions(
  gneiss_app_project_description_test
  PRIVATE GNEISS_APP_TEST_PROJECT="${PROJECT_SOURCE_DIR}/examples/editor_demo"
)
gneiss_target_compile_warnings(gneiss_app_project_description_test)
gneiss_target_output_directories(gneiss_app_project_description_test)
set_target_properties(gneiss_app_project_description_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.app.project-description COMMAND gneiss_app_project_description_test)

