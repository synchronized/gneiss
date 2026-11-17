# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
# 迁移阶段保留目标名；契约直接来自源 include，不再复制头文件。
foreach(target IN ITEMS gneiss_value_interface gneiss_native_interface gneiss_result_interface)
  add_library(${target} INTERFACE)
  target_include_directories(${target} INTERFACE "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/include>")
  target_compile_features(${target} INTERFACE cxx_std_20)
endforeach()
target_link_libraries(gneiss_value_interface INTERFACE gneiss_math_dependency)
target_link_libraries(gneiss_native_interface INTERFACE gneiss_value_interface gneiss_events)

add_library(gneiss_fatal STATIC "${PROJECT_SOURCE_DIR}/engine/src/fatal.cpp")
target_link_libraries(gneiss_fatal PUBLIC gneiss_result_interface)
set_target_properties(gneiss_fatal PROPERTIES POSITION_INDEPENDENT_CODE ON)
gneiss_target_compile_warnings(gneiss_fatal)
if(EMSCRIPTEN)
  target_compile_options(gneiss_fatal PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
  target_link_options(gneiss_fatal PUBLIC -fexceptions)
endif()
