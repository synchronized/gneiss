# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.25)
project(gneiss_task_web LANGUAGES CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "此最小宿主须使用 Emscripten 工具链")
endif()
set(GNEISS_TASK_THREADS OFF CACHE BOOL "" FORCE)
add_subdirectory(../../src/engine/core/tasks tasks)
target_compile_options(gneiss_tasks PRIVATE -fexceptions)
add_executable(task_web main.cpp)
target_link_libraries(task_web PRIVATE gneiss_tasks)
target_compile_options(task_web PRIVATE -fexceptions -Wall -Wextra -Werror)
target_link_options(task_web PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1)
set_target_properties(task_web PROPERTIES SUFFIX ".html")
