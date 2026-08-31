# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_player main.cpp)
target_link_libraries(gneiss_player PRIVATE gneiss_app_project gneiss::gneiss)
target_compile_features(gneiss_player PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_player)
gneiss_target_output_directories(gneiss_player)

if(BUILD_TESTING)
  add_test(
    NAME gneiss.player.smoke
    COMMAND gneiss_player --smoke --project "${PROJECT_SOURCE_DIR}/examples/editor_demo"
  )
  add_test(
    NAME gneiss.player.startup-diagnostic
    COMMAND
      "${CMAKE_COMMAND}" -DGNEISS_PLAYER=$<TARGET_FILE:gneiss_player>
      -DMISSING_PROJECT=${PROJECT_BINARY_DIR}/missing-player-project -P
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/startup_diagnostic_test.cmake"
  )
  set_tests_properties(gneiss.player.smoke PROPERTIES TIMEOUT 30)
  set_tests_properties(gneiss.player.startup-diagnostic PROPERTIES TIMEOUT 30)
endif()
