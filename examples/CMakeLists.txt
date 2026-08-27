# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_version_example version.cpp)
target_link_libraries(gneiss_version_example PRIVATE gneiss::gneiss)
gneiss_target_compile_warnings(gneiss_version_example)
gneiss_target_output_directories(gneiss_version_example)

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
