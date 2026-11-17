# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 数学使用要求独立传播，平台和任务不依赖 GLM。
add_library(gneiss_math INTERFACE)
add_library(gneiss::math ALIAS gneiss_math)
target_include_directories(gneiss_math INTERFACE
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/include>")
target_compile_features(gneiss_math INTERFACE cxx_std_20)
target_link_libraries(gneiss_math INTERFACE gneiss_math_dependency)

add_library(gneiss_events INTERFACE)
add_library(gneiss::events ALIAS gneiss_events)
target_include_directories(gneiss_events INTERFACE
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/include>")
target_compile_features(gneiss_events INTERFACE cxx_std_20)
target_link_libraries(gneiss_events INTERFACE EnTT::EnTT)

# 任务、IO 与宿主异常边界独立使用，不反向依赖 Engine 或普通日志系统。
add_library(gneiss_core STATIC ../fatal.cpp ../error.cpp)
add_library(gneiss::core ALIAS gneiss_core)
target_include_directories(gneiss_core PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/include>")
target_compile_features(gneiss_core PUBLIC cxx_std_20)
target_compile_definitions(gneiss_core INTERFACE
  $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_STATIC_DEFINE>)
set_target_properties(gneiss_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
gneiss_target_compile_warnings(gneiss_core)
if(EMSCRIPTEN)
  target_compile_options(gneiss_core PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
  target_link_options(gneiss_core PUBLIC -fexceptions)
endif()

add_subdirectory(tasks)
add_subdirectory(logging)
