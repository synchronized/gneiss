# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
include_guard(GLOBAL)
include(dependency_source_cache)
include(FetchContent)

# 两个上游均固定版本并使用统一源码缓存，禁止嵌套 FetchContent 绕过离线策略。
function(gneiss_resolve_libassert)
  if(TARGET gneiss_libassert)
    return()
  endif()
  gneiss_cached_archive(libassert "https://codeload.github.com/jeremy-rifkin/libassert/tar.gz/refs/tags/v2.2.1"
    "a7882ff2922c6d57f955f5ea9418014619e0855e936eb92e5443914dd1a8f724" CMakeLists.txt source)
  gneiss_cached_archive(cpptrace "https://codeload.github.com/jeremy-rifkin/cpptrace/tar.gz/refs/tags/v1.0.4"
    "5c9f5b301e903714a4d01f1057b9543fa540f7bfcc5e3f8bd1748e652e24f9ea" CMakeLists.txt trace_source)
  set(LIBASSERT_BUILD_SHARED OFF)
  set(CPPTRACE_BUILD_SHARED OFF)
  set(LIBASSERT_BUILD_TESTING OFF)
  set(CPPTRACE_BUILD_TESTING OFF)
  set(CPPTRACE_BUILD_TOOLS OFF)
  set(CPPTRACE_BUILD_BENCHMARKING OFF)
  set(LIBASSERT_DISABLE_CXX_20_MODULES ON)
  set(CPPTRACE_DISABLE_CXX_20_MODULES ON)
  set(LIBASSERT_USE_EXTERNAL_CPPTRACE OFF)
  set(LIBASSERT_USE_MAGIC_ENUM OFF)
  set(LIBASSERT_USE_ENCHANTUM OFF)
  # Windows 使用系统 DbgHelp；桌面 Unix 使用 addr2line/atos，不另下载 dwarf/zstd。
  if(EMSCRIPTEN)
    set(CPPTRACE_UNWIND_WITH_NOTHING ON)
    set(CPPTRACE_GET_SYMBOLS_WITH_NOTHING ON)
    set(CPPTRACE_DEMANGLE_WITH_NOTHING ON)
  elseif(WIN32)
    set(CPPTRACE_GET_SYMBOLS_WITH_DBGHELP ON)
  else()
    set(CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE ON)
    set(CPPTRACE_ADDR2LINE_SEARCH_SYSTEM_PATH ON)
  endif()
  FetchContent_Declare(cpptrace SOURCE_DIR "${trace_source}")
  FetchContent_Declare(libassert SOURCE_DIR "${source}")
  FetchContent_MakeAvailable(libassert)
  add_library(gneiss_libassert ALIAS libassert-lib)
  set_target_properties(libassert-lib cpptrace-lib PROPERTIES POSITION_INDEPENDENT_CODE ON
    FOLDER "ThirdParty/Diagnostics")
  target_compile_definitions(libassert-lib INTERFACE LIBASSERT_PREFIX_ASSERTIONS)
  install(FILES "${source}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
    RENAME libassert-LICENSE)
  install(FILES "${trace_source}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
    RENAME cpptrace-LICENSE)
endfunction()

gneiss_resolve_libassert()
