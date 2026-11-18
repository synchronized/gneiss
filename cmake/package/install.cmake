# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(EMSCRIPTEN)
  install(FILES "${PROJECT_SOURCE_DIR}/engine/src/platform/files/backends/detail/fetch_stream.js"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/js")
  target_link_options(gneiss_engine INTERFACE
    "$<INSTALL_INTERFACE:SHELL:--js-library \"$<INSTALL_PREFIX>/${CMAKE_INSTALL_DATADIR}/gneiss/js/fetch_stream.js\">")
endif()


if(DEFINED GNEISS_LIBUV_SOURCE_DIR)
  install(
    FILES "${GNEISS_LIBUV_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
    RENAME libuv-LICENSE
  )
endif()

set(GNEISS_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/gneiss")
configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/package/gneissConfig.cmake.in"
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
  TARGETS gneiss_engine gneiss_math gneiss_base gneiss_data
    gneiss_meta gneiss_serialization gneiss_serialization_json
  EXPORT gneissTargets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(
  FILES
    "${GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR}/libraries/pbr_standard.grshlib"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/runtime-assets/granit/libraries"
)
install(
  FILES "${GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR}/materials/pbr_standard.grmat"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/runtime-assets/granit/materials"
)
if(TARGET gneiss_runtime)
  install(
    TARGETS gneiss_runtime
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
  foreach(
    granit_runtime_target
    IN ITEMS granit::granit granit::window granit::render_pipeline
  )
    if(TARGET ${granit_runtime_target})
      get_target_property(granit_runtime_type ${granit_runtime_target} TYPE)
      if(granit_runtime_type STREQUAL "SHARED_LIBRARY" OR
         granit_runtime_type STREQUAL "MODULE_LIBRARY")
        if(WIN32)
          set(granit_runtime_destination "${CMAKE_INSTALL_BINDIR}")
        else()
          set(granit_runtime_destination "${CMAKE_INSTALL_LIBDIR}")
        endif()
        install(
          FILES "$<TARGET_FILE:${granit_runtime_target}>"
          DESTINATION "${granit_runtime_destination}"
        )
      endif()
    endif()
  endforeach()
endif()
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
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/templates/game/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/templates/game"
)
install(FILES 3rd/entt/LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
        RENAME EnTT-LICENSE)
install(FILES 3rd/yyjson/LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
        RENAME yyjson-LICENSE)
install(FILES 3rd/libspng/LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
        RENAME libspng-LICENSE)
install(FILES 3rd/miniz/LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses"
        RENAME miniz-LICENSE)
install(FILES "${PROJECT_SOURCE_DIR}/3rd/granit-example-assets-LICENSE"
              "${PROJECT_SOURCE_DIR}/3rd/caors-core-values-LICENSE"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses")
install(FILES "${GNEISS_GLM_SOURCE_DIR}/copying.txt" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gneiss/licenses" RENAME GLM-LICENSE)
