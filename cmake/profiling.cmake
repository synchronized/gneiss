# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include_guard(GLOBAL)
option(GNEISS_ENABLE_PROFILING "启用可选 Tracy CPU 性能采集" OFF)

if(GNEISS_ENABLE_PROFILING)
  if(EMSCRIPTEN)
    message(FATAL_ERROR "Tracy profiling 配置当前仅用于原生宿主")
  endif()
  include(FetchContent)
  FetchContent_Declare(gneiss_tracy_source
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG 05cceee0df3b8d7c6fa87e9638af311dbabc63cb
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR gneiss-no-cmake)
  FetchContent_MakeAvailable(gneiss_tracy_source)
  # 一个进程共用该共享 client，不能在每个静态模块里分别编译 TracyClient.cpp。
  add_library(gneiss_tracy SHARED "${gneiss_tracy_source_SOURCE_DIR}/public/TracyClient.cpp")
  target_compile_features(gneiss_tracy PRIVATE cxx_std_20)
  target_compile_definitions(gneiss_tracy PRIVATE TRACY_ENABLE TRACY_EXPORTS
    TRACY_ON_DEMAND TRACY_ONLY_LOCALHOST TRACY_NO_BROADCAST)
  find_package(Threads REQUIRED)
  target_link_libraries(gneiss_tracy PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  if(WIN32)
    target_link_libraries(gneiss_tracy PRIVATE ws2_32 dbghelp secur32)
  endif()
  gneiss_target_output_directories(gneiss_tracy)
  include(GNUInstallDirs)
  install(TARGETS gneiss_tracy EXPORT gneissTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  install(FILES "${gneiss_tracy_source_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses" RENAME tracy-LICENSE)
endif()

function(gneiss_target_profiling target)
  if(GNEISS_ENABLE_PROFILING)
    target_link_libraries(${target} PRIVATE gneiss_tracy)
    target_include_directories(${target} SYSTEM PRIVATE "${gneiss_tracy_source_SOURCE_DIR}/public")
    target_compile_definitions(${target} PRIVATE GNEISS_ENABLE_PROFILING TRACY_ENABLE TRACY_IMPORTS
      TRACY_ON_DEMAND TRACY_ONLY_LOCALHOST TRACY_NO_BROADCAST)
  endif()
endfunction()
