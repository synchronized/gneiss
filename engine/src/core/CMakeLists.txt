# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
add_library(gneiss_events INTERFACE)
target_link_libraries(gneiss_events INTERFACE gneiss_result_interface EnTT::EnTT)
add_library(gneiss_core_values INTERFACE)
target_link_libraries(gneiss_core_values INTERFACE gneiss_value_interface)
target_compile_definitions(gneiss_core_values INTERFACE
  $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_STATIC_DEFINE>)
add_subdirectory(tasks)
add_subdirectory(logging)
