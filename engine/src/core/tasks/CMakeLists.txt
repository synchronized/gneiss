# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

option(GNEISS_TASK_THREADS "启用任务工作线程后端" ON)
add_library(gneiss_tasks STATIC task_scheduler.cpp)
add_library(gneiss::tasks ALIAS gneiss_tasks)
if(COMMAND gneiss_target_profiling)
  gneiss_target_profiling(gneiss_tasks)
endif()
target_link_libraries(gneiss_tasks PRIVATE gneiss_core)
target_include_directories(gneiss_tasks PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../include>")

target_include_directories(gneiss_tasks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
target_compile_features(gneiss_tasks PUBLIC cxx_std_20)
target_compile_definitions(gneiss_tasks PRIVATE GNEISS_TASK_THREADS=$<BOOL:${GNEISS_TASK_THREADS}>)
if(GNEISS_TASK_THREADS)
  find_package(Threads REQUIRED)
  target_link_libraries(gneiss_tasks PRIVATE Threads::Threads)
endif()

gneiss_target_compile_warnings(gneiss_tasks)
gneiss_target_output_directories(gneiss_tasks)