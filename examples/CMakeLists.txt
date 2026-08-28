# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_version_example version.cpp)
target_link_libraries(gneiss_version_example PRIVATE gneiss::gneiss)
gneiss_target_compile_warnings(gneiss_version_example)
gneiss_target_output_directories(gneiss_version_example)

add_executable(gneiss_property_inspector_example property_inspector/main.cpp)
target_link_libraries(gneiss_property_inspector_example PRIVATE gneiss::gneiss)
target_compile_definitions(
  gneiss_property_inspector_example
  PRIVATE
    GNEISS_PROPERTY_INSPECTOR_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/property_inspector/assets"
)
gneiss_target_compile_warnings(gneiss_property_inspector_example)
gneiss_target_output_directories(gneiss_property_inspector_example)
install(TARGETS gneiss_property_inspector_example RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/property_inspector/assets/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/examples/property-inspector/assets"
)
if(BUILD_TESTING)
  add_test(NAME gneiss.property_inspector_example COMMAND gneiss_property_inspector_example)
endif()

if(GNEISS_ENABLE_GRANIT_PLATFORM)
  add_executable(gneiss_temple_example temple/main.cpp)
  target_link_libraries(gneiss_temple_example PRIVATE gneiss::gneiss)
  target_compile_definitions(
    gneiss_temple_example
    PRIVATE GNEISS_TEMPLE_SOURCE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/temple/assets"
  )
  gneiss_target_compile_warnings(gneiss_temple_example)
  gneiss_target_output_directories(gneiss_temple_example)
  install(TARGETS gneiss_temple_example RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/temple/assets/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/examples/temple/assets"
  )
  if(BUILD_TESTING)
    add_test(NAME gneiss.temple_example COMMAND gneiss_temple_example --smoke)
  endif()
endif()

if(GNEISS_ENABLE_GRANIT_PLATFORM AND TARGET gneiss_assetc)
  set(lantern_gallery_source_root "${CMAKE_CURRENT_SOURCE_DIR}/lantern_gallery/assets")
  set(lantern_gallery_asset_root "${CMAKE_CURRENT_BINARY_DIR}/lantern_gallery/assets")
  set(lantern_gallery_stamp "${CMAKE_CURRENT_BINARY_DIR}/lantern_gallery-assets.stamp")
  file(
    GLOB_RECURSE lantern_gallery_authored_assets
    CONFIGURE_DEPENDS
    "${lantern_gallery_source_root}/authored/*"
  )
  add_custom_command(
    OUTPUT "${lantern_gallery_stamp}"
    COMMAND
      "$<TARGET_FILE:gneiss_assetc>" import
      "${lantern_gallery_source_root}/source/Lantern.glb" --output
      "${lantern_gallery_asset_root}"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_directory "${lantern_gallery_source_root}/authored"
      "${lantern_gallery_asset_root}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${lantern_gallery_stamp}"
    DEPENDS
      gneiss_assetc
      "${lantern_gallery_source_root}/source/Lantern.glb"
      ${lantern_gallery_authored_assets}
    COMMENT "导入 Lantern GLB 并组装灯廊示例资产"
    VERBATIM
  )
  add_custom_target(gneiss_lantern_gallery_assets ALL DEPENDS "${lantern_gallery_stamp}")
  add_executable(gneiss_lantern_gallery_example lantern_gallery/main.cpp)
  add_dependencies(gneiss_lantern_gallery_example gneiss_lantern_gallery_assets)
  target_link_libraries(gneiss_lantern_gallery_example PRIVATE gneiss::gneiss)
  target_compile_definitions(
    gneiss_lantern_gallery_example
    PRIVATE GNEISS_LANTERN_GALLERY_ASSET_ROOT="${lantern_gallery_asset_root}"
  )
  gneiss_target_compile_warnings(gneiss_lantern_gallery_example)
  gneiss_target_output_directories(gneiss_lantern_gallery_example)
  install(TARGETS gneiss_lantern_gallery_example RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(
    DIRECTORY "${lantern_gallery_asset_root}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/examples/lantern-gallery/assets"
  )
  install(
    FILES
      "${lantern_gallery_source_root}/ASSET_ORIGINS.md"
      "${lantern_gallery_source_root}/source/LICENSE.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/examples/lantern-gallery"
  )
  if(BUILD_TESTING)
    add_test(NAME gneiss.lantern_gallery_example COMMAND gneiss_lantern_gallery_example --smoke)
  endif()
elseif(GNEISS_ENABLE_GRANIT_PLATFORM)
  message(STATUS "GNEISS_BUILD_TOOLS=OFF，跳过需要离线导入的 Lantern 示例")
endif()
