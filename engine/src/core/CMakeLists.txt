# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 基础值类型的头文件使用要求；不链接完整 Engine。
add_library(gneiss_core_values INTERFACE)
target_include_directories(gneiss_core_values INTERFACE
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/include>")
target_compile_features(gneiss_core_values INTERFACE cxx_std_20)
target_link_libraries(gneiss_core_values INTERFACE gneiss_math_dependency)
target_compile_definitions(gneiss_core_values INTERFACE
  $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_STATIC_DEFINE>)

add_library(gneiss_events INTERFACE)
target_include_directories(gneiss_events INTERFACE
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/include>")
target_compile_features(gneiss_events INTERFACE cxx_std_20)
target_link_libraries(gneiss_events INTERFACE EnTT::EnTT)

# 任务、IO 与宿主异常边界独立使用，不反向依赖 Engine 或普通日志系统。
add_library(gneiss_fatal STATIC ../fatal.cpp)
target_include_directories(gneiss_fatal PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/include>")
target_compile_features(gneiss_fatal PUBLIC cxx_std_20)
set_target_properties(gneiss_fatal PROPERTIES POSITION_INDEPENDENT_CODE ON)
gneiss_target_compile_warnings(gneiss_fatal)
if(EMSCRIPTEN)
  target_compile_options(gneiss_fatal PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
  target_link_options(gneiss_fatal PUBLIC -fexceptions)
endif()

add_subdirectory(tasks)
add_subdirectory(logging)
