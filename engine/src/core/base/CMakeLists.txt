# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 共享构建的身份表与字符串池由此唯一动态库持有。
add_library(gneiss_base ../../fatal.cpp ../../error.cpp
  ../types/guid.cpp ../types/string_name.cpp ../types/type_id.cpp ../sha256.cpp)
add_library(gneiss::base ALIAS gneiss_base)
target_include_directories(gneiss_base PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../include>")
target_compile_features(gneiss_base PUBLIC cxx_std_20)
target_compile_definitions(gneiss_base PRIVATE GNEISS_BUILDING_BASE
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_BASE_STATIC_DEFINE>
         $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_STATIC_DEFINE>)
set_target_properties(gneiss_base PROPERTIES EXPORT_NAME base POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
gneiss_target_compile_warnings(gneiss_base)
gneiss_target_output_directories(gneiss_base)
if(EMSCRIPTEN)
  target_compile_options(gneiss_base PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
  target_link_options(gneiss_base PUBLIC -fexceptions)
endif()
