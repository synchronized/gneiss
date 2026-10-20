# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

if(NOT TARGET gneiss_io_uv)
  message(FATAL_ERROR "CAORS probe 需要原生 libuv 与任务调度模块")
endif()
include(FetchContent)
# MIT；只链接验证程序，不传播到 Gneiss SDK。固定已评审的 0.4.0 提交。
set(CAORS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CAORS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CAORS_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(CAORS_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
FetchContent_Declare(gneiss_caors
  GIT_REPOSITORY https://github.com/synchronized/caors-core.git
  GIT_TAG cbe41b75223f52bec746cc23346b28eba90fa693
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(gneiss_caors)
add_library(gneiss_caors_adapter STATIC async_file.cpp file_system.cpp worker.cpp)
target_compile_features(gneiss_caors_adapter PUBLIC cxx_std_20)
target_include_directories(gneiss_caors_adapter PRIVATE "${PROJECT_SOURCE_DIR}/src")
# 只供内部宿主使用，既不安装也不链接 Engine DLL。
target_link_libraries(gneiss_caors_adapter PUBLIC caors::caors PRIVATE gneiss_io_uv gneiss_tasks)
gneiss_target_compile_warnings(gneiss_caors_adapter)
gneiss_target_output_directories(gneiss_caors_adapter)
