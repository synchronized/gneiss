# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_minimal_example minimal/main.cpp)
target_link_libraries(gneiss_minimal_example PRIVATE gneiss_fatal gneiss::engine)
gneiss_target_compile_warnings(gneiss_minimal_example)
gneiss_target_output_directories(gneiss_minimal_example)
set_target_properties(gneiss_minimal_example PROPERTIES FOLDER "Examples")

add_executable(gneiss_scene_viewer scene_viewer/main.cpp)
target_link_libraries(gneiss_scene_viewer PRIVATE gneiss_fatal gneiss::engine gneiss_tasks)
target_compile_definitions(gneiss_scene_viewer PRIVATE
  GNEISS_VIEWER_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/temple/assets")
gneiss_target_compile_warnings(gneiss_scene_viewer)
gneiss_target_output_directories(gneiss_scene_viewer)
set_target_properties(gneiss_scene_viewer PROPERTIES FOLDER "Examples")
install(TARGETS gneiss_scene_viewer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/temple/assets/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/examples/scene-viewer/assets")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/editor_demo/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/projects/editor-demo")
if(BUILD_TESTING)
  add_test(NAME gneiss.minimal-example COMMAND gneiss_minimal_example)
  set_tests_properties(gneiss.minimal-example PROPERTIES LABELS "cpu")
  add_test(NAME gneiss.scene-viewer COMMAND gneiss_scene_viewer --smoke)
  set_tests_properties(gneiss.scene-viewer PROPERTIES LABELS "gpu;integration" TIMEOUT 60)
  add_test(NAME gneiss.scene-viewer-dual COMMAND gneiss_scene_viewer --smoke --dual-view)
  set_tests_properties(gneiss.scene-viewer-dual PROPERTIES LABELS "gpu;integration" TIMEOUT 60)
endif()

if(TARGET gneiss_assetc)
  set(lantern_gallery_source_root "${CMAKE_CURRENT_SOURCE_DIR}/lantern_gallery/assets")
  set(lantern_gallery_project_root "${CMAKE_CURRENT_BINARY_DIR}/lantern_gallery")
  set(lantern_gallery_asset_root "${lantern_gallery_project_root}/assets")
  set(lantern_gallery_stamp "${CMAKE_CURRENT_BINARY_DIR}/lantern_gallery-assets.stamp")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/lantern_gallery/gneiss.project.json"
    "${lantern_gallery_project_root}/gneiss.project.json"
    COPYONLY
  )
  file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}" GNEISS_LANTERN_BUILD_TREE)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/lantern_gallery/CMakePresets.build-tree.json.in"
    "${lantern_gallery_project_root}/CMakePresets.json"
    @ONLY
  )
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
  install(
    DIRECTORY "${lantern_gallery_asset_root}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/examples/lantern-gallery/assets"
  )
  install(
    FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/lantern_gallery/gneiss.project.json"
      "${lantern_gallery_source_root}/ASSET_ORIGINS.md"
      "${lantern_gallery_source_root}/source/LICENSE.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/gneiss/examples/lantern-gallery"
  )
  if(BUILD_TESTING)
    add_test(NAME gneiss.viewer-lantern COMMAND gneiss_scene_viewer --smoke
      --assets "${lantern_gallery_asset_root}" --scene "asset://scenes/gallery.scene.json")
    set_tests_properties(gneiss.viewer-lantern PROPERTIES LABELS "gpu;integration" TIMEOUT 60)
    if(TARGET gneiss_runtime AND TARGET gneiss_lantern_gallery_game)
      add_dependencies(gneiss_runtime gneiss_lantern_gallery_game)
      add_test(
        NAME gneiss.runtime.lantern-gallery
        COMMAND gneiss_runtime --smoke --project "${lantern_gallery_project_root}"
      )
      set_tests_properties(gneiss.runtime.lantern-gallery PROPERTIES TIMEOUT 30)
      if(TARGET gneiss_corrupt_texture_fixture)
        add_dependencies(gneiss_runtime gneiss_corrupt_texture_fixture)
        add_test(
          NAME gneiss.runtime.lantern-cooked-textures
          COMMAND "${CMAKE_COMMAND}"
            -DGNEISS_ASSETC=$<TARGET_FILE:gneiss_assetc>
            -DGNEISS_RUNTIME=$<TARGET_FILE:gneiss_runtime>
            -DGNEISS_CORRUPT=$<TARGET_FILE:gneiss_corrupt_texture_fixture>
            -DGNEISS_PROJECT=${lantern_gallery_project_root}
            -DGNEISS_OUTPUT=${PROJECT_BINARY_DIR}/lantern-cooked-test
            -P "${PROJECT_SOURCE_DIR}/cmake/testing/check_lantern_cooked.cmake"
        )
        set_tests_properties(gneiss.runtime.lantern-cooked-textures PROPERTIES TIMEOUT 210)
      endif()
    endif()
    if(TARGET gneiss_editor)
      add_test(
        NAME gneiss.editor.lantern-gallery-project
        COMMAND gneiss_editor --smoke --project "${lantern_gallery_project_root}"
      )
      set_tests_properties(gneiss.editor.lantern-gallery-project PROPERTIES
        TIMEOUT 30 FAIL_REGULAR_EXPRESSION "资产监听启动失败"
      )
      add_executable(
        gneiss_editor_lantern_workflow_test
        "${PROJECT_SOURCE_DIR}/tests/editor/lantern_workflow_test.cpp"
      )
      target_link_libraries(
        gneiss_editor_lantern_workflow_test
        PRIVATE gneiss_editor_assets gneiss_editor_host
      )
      target_compile_definitions(
        gneiss_editor_lantern_workflow_test
        PRIVATE
          GNEISS_EDITOR_LANTERN_PROJECT="${lantern_gallery_project_root}"
          GNEISS_EDITOR_LANTERN_SOURCE="${lantern_gallery_source_root}/source/Lantern.glb"
      )
      target_include_directories(gneiss_editor_lantern_workflow_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
      add_dependencies(gneiss_editor_lantern_workflow_test gneiss_lantern_gallery_assets)
      gneiss_target_compile_warnings(gneiss_editor_lantern_workflow_test)
      gneiss_target_output_directories(gneiss_editor_lantern_workflow_test)
      set_target_properties(gneiss_editor_lantern_workflow_test PROPERTIES FOLDER "Tests")
      add_test(NAME gneiss.editor.lantern-workflow COMMAND gneiss_editor_lantern_workflow_test)
      add_executable(
        gneiss_editor_lantern_prefab_authoring_workflow_test
        "${PROJECT_SOURCE_DIR}/tests/editor/lantern_prefab_authoring_workflow_test.cpp"
      )
      target_link_libraries(
        gneiss_editor_lantern_prefab_authoring_workflow_test PRIVATE gneiss_editor_host
      )
      target_compile_definitions(
        gneiss_editor_lantern_prefab_authoring_workflow_test
        PRIVATE GNEISS_EDITOR_LANTERN_PROJECT="${lantern_gallery_project_root}"
      )
      add_dependencies(
        gneiss_editor_lantern_prefab_authoring_workflow_test gneiss_lantern_gallery_assets
      )
      gneiss_target_compile_warnings(gneiss_editor_lantern_prefab_authoring_workflow_test)
      gneiss_target_output_directories(gneiss_editor_lantern_prefab_authoring_workflow_test)
      set_target_properties(gneiss_editor_lantern_prefab_authoring_workflow_test PROPERTIES FOLDER "Tests")
      add_test(
        NAME gneiss.editor.lantern-prefab-authoring-workflow
        COMMAND gneiss_editor_lantern_prefab_authoring_workflow_test
      )
      if(TARGET gneiss_runtime AND TARGET gneiss_lantern_gallery_game)
        add_executable(
          gneiss_editor_lantern_runtime_workflow_test
          "${PROJECT_SOURCE_DIR}/tests/editor/lantern_runtime_workflow_test.cpp"
        )
        target_link_libraries(
          gneiss_editor_lantern_runtime_workflow_test PRIVATE gneiss_editor_host
        )
        target_compile_definitions(
          gneiss_editor_lantern_runtime_workflow_test
          PRIVATE
            GNEISS_LANTERN_RUNTIME="$<TARGET_FILE:gneiss_runtime>"
            GNEISS_LANTERN_PROJECT="${lantern_gallery_project_root}"
        )
        add_dependencies(
          gneiss_editor_lantern_runtime_workflow_test
          gneiss_runtime
          gneiss_lantern_gallery_game
        )
        gneiss_target_compile_warnings(gneiss_editor_lantern_runtime_workflow_test)
        gneiss_target_output_directories(gneiss_editor_lantern_runtime_workflow_test)
        set_target_properties(gneiss_editor_lantern_runtime_workflow_test PROPERTIES FOLDER "Tests")
        add_test(
          NAME gneiss.editor.lantern-runtime-workflow
          COMMAND gneiss_editor_lantern_runtime_workflow_test
        )
        set_tests_properties(gneiss.editor.lantern-runtime-workflow PROPERTIES TIMEOUT 20)
      endif()
    endif()
  endif()
else()
  message(STATUS "GNEISS_BUILD_TOOLS=OFF，跳过需要离线导入的 Lantern 示例")
endif()

# 异步场景测试由宿主驱动任务，不使用生产同步加载入口。
foreach(test IN ITEMS gneiss_editor_lantern_workflow_test gneiss_editor_lantern_prefab_authoring_workflow_test)
  if(TARGET ${test})
    target_link_libraries(${test} PRIVATE gneiss_tasks)
    target_include_directories(${test} PRIVATE "${PROJECT_SOURCE_DIR}/src")
  endif()
endforeach()
