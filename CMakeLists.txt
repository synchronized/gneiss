# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)

project(
  gneiss
  VERSION 0.10.0
  DESCRIPTION "基于 Granit 构建的现代 C++20 游戏引擎"
  LANGUAGES C CXX
)

option(BUILD_SHARED_LIBS "构建共享库" ON)
option(GNEISS_BUILD_TESTING "构建 Gneiss 测试" ON)
option(GNEISS_BUILD_EXAMPLES "构建 Gneiss 示例" ${PROJECT_IS_TOP_LEVEL})
option(GNEISS_BUILD_TOOLS "构建 Gneiss 离线工具" ${PROJECT_IS_TOP_LEVEL})
option(GNEISS_BUILD_EDITOR "构建 Gneiss Editor" OFF)
option(GNEISS_ENABLE_GRANIT_PLATFORM "启用 Granit Window 平台适配" OFF)

if(GNEISS_BUILD_EDITOR AND NOT GNEISS_ENABLE_GRANIT_PLATFORM)
  message(FATAL_ERROR "GNEISS_BUILD_EDITOR=ON 需要 GNEISS_ENABLE_GRANIT_PLATFORM=ON")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(gneiss_utility)
if(GNEISS_ENABLE_GRANIT_PLATFORM)
  include(granit)
  gneiss_resolve_granit_runtime()
endif()
include(CTest)

add_subdirectory(3rd)
add_subdirectory(src)
if(GNEISS_BUILD_TOOLS)
  include(fastgltf)
  gneiss_resolve_fastgltf()
  add_subdirectory(tools)
endif()
if(GNEISS_BUILD_EDITOR)
  include(imgui)
  gneiss_resolve_imgui()
  include(imguizmo)
  gneiss_resolve_imguizmo()
  add_subdirectory(apps/editor)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(GNEISS_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/gneiss")
configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/gneissConfig.cmake.in"
  "${PROJECT_BINARY_DIR}/gneissConfig.cmake"
  INSTALL_DESTINATION "${GNEISS_INSTALL_CMAKEDIR}"
  PATH_VARS CMAKE_INSTALL_BINDIR CMAKE_INSTALL_LIBDIR
)
write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/gneissConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)
install(
  TARGETS gneiss
  EXPORT gneissTargets
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
  EXPORT gneissTargets
  FILE gneissTargets.cmake
  NAMESPACE gneiss::
  DESTINATION "${GNEISS_INSTALL_CMAKEDIR}"
)
install(
  FILES
    "${PROJECT_BINARY_DIR}/gneissConfig.cmake"
    "${PROJECT_BINARY_DIR}/gneissConfigVersion.cmake"
  DESTINATION "${GNEISS_INSTALL_CMAKEDIR}"
)
install(
  FILES LICENSE CHANGELOG.md THIRD_PARTY_NOTICES.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss"
)
install(FILES 3rd/entt/LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
        RENAME EnTT-LICENSE)
install(FILES 3rd/yyjson/LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
        RENAME yyjson-LICENSE)
install(FILES 3rd/libspng/LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
        RENAME libspng-LICENSE)
install(FILES 3rd/miniz/LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
        RENAME miniz-LICENSE)
if(GNEISS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(GNEISS_BUILD_TESTING AND BUILD_TESTING)
  add_subdirectory(tests)
endif()
