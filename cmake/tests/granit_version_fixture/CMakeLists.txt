# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.24)
project(gneiss_granit_version_fixture NONE)

if(NOT DEFINED GNEISS_SOURCE_DIR)
  message(FATAL_ERROR "缺少 GNEISS_SOURCE_DIR")
endif()

include("${GNEISS_SOURCE_DIR}/cmake/granit_version.cmake")
