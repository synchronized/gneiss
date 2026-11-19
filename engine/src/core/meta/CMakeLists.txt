# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
add_library(gneiss_meta registry.cpp instance.cpp object.cpp resolve_path.cpp)
add_library(gneiss::meta ALIAS gneiss_meta)
target_link_libraries(gneiss_meta PUBLIC gneiss_data PRIVATE gneiss_libassert)
target_compile_definitions(gneiss_meta PRIVATE GNEISS_BUILDING_META
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_META_STATIC_DEFINE>)
set_target_properties(gneiss_meta PROPERTIES EXPORT_NAME meta POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
gneiss_target_compile_warnings(gneiss_meta)
gneiss_target_output_directories(gneiss_meta)
