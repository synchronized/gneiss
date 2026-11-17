# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_cpp_header_test OBJECT  asset_hpp.cpp entity_hpp.cpp
                                  input_hpp.cpp log_hpp.cpp
                                 result_hpp.cpp rid_hpp.cpp
                                scene_hpp.cpp
)
target_link_libraries(gneiss_cpp_header_test PRIVATE gneiss::engine gneiss_native_interface)
target_compile_features(gneiss_cpp_header_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_header_test)
set_target_properties(gneiss_cpp_header_test PROPERTIES FOLDER "Tests")
