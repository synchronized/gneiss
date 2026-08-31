# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_app_project STATIC
  project_description.cpp
  include/gneiss/app/project_description.h
)
target_link_libraries(gneiss_app_project PRIVATE gneiss_yyjson PUBLIC gneiss::engine)
target_include_directories(
  gneiss_app_project
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(gneiss_app_project PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_app_project)
gneiss_target_output_directories(gneiss_app_project)

if(BUILD_TESTING)
  add_executable(
    gneiss_app_project_description_test
    tests/project_description_test.cpp
  )
  target_link_libraries(gneiss_app_project_description_test PRIVATE gneiss_app_project)
  target_compile_definitions(
    gneiss_app_project_description_test
    PRIVATE GNEISS_APP_TEST_PROJECT="${PROJECT_SOURCE_DIR}/examples/editor_demo"
  )
  gneiss_target_compile_warnings(gneiss_app_project_description_test)
  gneiss_target_output_directories(gneiss_app_project_description_test)
  add_test(NAME gneiss.app.project-description COMMAND gneiss_app_project_description_test)
endif()
