# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)

project(gneiss_lantern_gallery_game LANGUAGES CXX)

find_package(gneiss CONFIG REQUIRED)

get_target_property(gneiss_engine_type gneiss::engine TYPE)
if(NOT gneiss_engine_type STREQUAL "SHARED_LIBRARY")
  message(FATAL_ERROR "原生 Game Module 需要 Shared Gneiss Engine SDK")
endif()

add_library(gneiss_lantern_gallery_game MODULE game_module.cpp)
target_link_libraries(gneiss_lantern_gallery_game PRIVATE gneiss::engine)
target_compile_definitions(gneiss_lantern_gallery_game PRIVATE GNEISS_BUILDING_GAME_MODULE)
target_compile_features(gneiss_lantern_gallery_game PRIVATE cxx_std_20)
set_target_properties(
  gneiss_lantern_gallery_game
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/modules>"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/modules>"
)
if(WIN32)
  set_target_properties(gneiss_lantern_gallery_game PROPERTIES PREFIX "")
endif()

if(MSVC)
  target_compile_options(gneiss_lantern_gallery_game PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(
    gneiss_lantern_gallery_game
    PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror
  )
endif()
