# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_version_example version.cpp)
target_link_libraries(gneiss_version_example PRIVATE gneiss::gneiss)
gneiss_target_compile_warnings(gneiss_version_example)
gneiss_target_output_directories(gneiss_version_example)

if(GNEISS_ENABLE_GRANIT_PLATFORM)
  add_executable(gneiss_temple_example temple/main.cpp)
  target_link_libraries(gneiss_temple_example PRIVATE gneiss::gneiss)
  gneiss_target_compile_warnings(gneiss_temple_example)
  gneiss_target_output_directories(gneiss_temple_example)
  if(BUILD_TESTING)
    add_test(NAME gneiss.temple_example COMMAND gneiss_temple_example --smoke)
    set_tests_properties(gneiss.temple_example PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  endif()
endif()
