# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_subdirectory(base)
add_subdirectory(math)

add_library(gneiss_events INTERFACE)
add_library(gneiss::events ALIAS gneiss_events)
target_include_directories(gneiss_events INTERFACE
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/include>")
target_compile_features(gneiss_events INTERFACE cxx_std_20)
target_link_libraries(gneiss_events INTERFACE EnTT::EnTT)

add_subdirectory(tasks)
add_subdirectory(logging)
