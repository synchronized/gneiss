# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_version_example version.cpp)
target_link_libraries(gneiss_version_example PRIVATE gneiss::gneiss)
gneiss_target_compile_warnings(gneiss_version_example)
gneiss_target_output_directories(gneiss_version_example)

