# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
include_guard(GLOBAL)
include(dependency_source_cache)

# 只供同步输出适配器私有链接，不启用上游 logger、线程池或 fmt。
function(gneiss_resolve_spdlog)
  if(TARGET gneiss_spdlog)
    return()
  endif()
  gneiss_cached_archive(spdlog "https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.15.3"
    "15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67" CMakeLists.txt source)
  add_library(gneiss_spdlog INTERFACE)
  target_include_directories(gneiss_spdlog SYSTEM INTERFACE "${source}/include")
  target_compile_features(gneiss_spdlog INTERFACE cxx_std_20)
  target_compile_definitions(gneiss_spdlog INTERFACE SPDLOG_USE_STD_FORMAT
    $<$<PLATFORM_ID:Windows>:SPDLOG_WCHAR_FILENAMES>)
  install(FILES "${source}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
    RENAME spdlog-LICENSE)
endfunction()
