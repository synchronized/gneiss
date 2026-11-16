# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
# Editor 宿主、进程与资产工作流测试。

if(BUILD_TESTING)
  add_executable(gneiss_editor_scene_loader_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_scene_loader_test.cpp")
  add_executable(gneiss_editor_prefab_edit_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_prefab_edit_test.cpp")
  target_link_libraries(gneiss_editor_prefab_edit_test PRIVATE gneiss_scene_document gneiss_tasks)
  gneiss_target_compile_warnings(gneiss_editor_prefab_edit_test)
  gneiss_target_output_directories(gneiss_editor_prefab_edit_test)
  set_target_properties(gneiss_editor_prefab_edit_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.prefab-edit COMMAND gneiss_editor_prefab_edit_test)
  set_tests_properties(gneiss.editor.prefab-edit PROPERTIES TIMEOUT 45)
  add_executable(gneiss_editor_asset_edit_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_asset_edit_test.cpp")
  target_link_libraries(gneiss_editor_asset_edit_test PRIVATE gneiss_scene_document gneiss_tasks)
  gneiss_target_compile_warnings(gneiss_editor_asset_edit_test)
  gneiss_target_output_directories(gneiss_editor_asset_edit_test)
  set_target_properties(gneiss_editor_asset_edit_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.asset-edit COMMAND gneiss_editor_asset_edit_test)
  target_link_libraries(gneiss_editor_scene_loader_test PRIVATE gneiss_scene_document gneiss_tasks)
  target_compile_definitions(gneiss_editor_scene_loader_test PRIVATE
    GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets")
  gneiss_target_compile_warnings(gneiss_editor_scene_loader_test)
  gneiss_target_output_directories(gneiss_editor_scene_loader_test)
  set_target_properties(gneiss_editor_scene_loader_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.scene-loader COMMAND gneiss_editor_scene_loader_test)
  if(WIN32)
    add_executable(gneiss_editor_native_window_interaction_test "${PROJECT_SOURCE_DIR}/tests/editor/native_window_interaction_test.cpp")
    target_link_libraries(gneiss_editor_native_window_interaction_test PRIVATE gneiss_editor_ui user32)
    if(TARGET gneiss_editor_assets)
      target_link_libraries(gneiss_editor_native_window_interaction_test PRIVATE gneiss_editor_assets gneiss_editor_host)
      target_compile_definitions(gneiss_editor_native_window_interaction_test PRIVATE GNEISS_TEST_BACKGROUND_ASSETS)
    endif()
    gneiss_target_compile_warnings(gneiss_editor_native_window_interaction_test)
    gneiss_target_output_directories(gneiss_editor_native_window_interaction_test)
    set_target_properties(gneiss_editor_native_window_interaction_test PROPERTIES FOLDER "Tests")
    add_test(NAME gneiss.editor.native-window-interaction COMMAND gneiss_editor_native_window_interaction_test)
    set_tests_properties(gneiss.editor.native-window-interaction PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
  endif()
  add_executable(gneiss_editor_gizmo_input_replay_test "${PROJECT_SOURCE_DIR}/tests/editor/gizmo_input_replay_test.cpp")
  target_link_libraries(gneiss_editor_gizmo_input_replay_test PRIVATE gneiss_editor_model gneiss_imguizmo)
  gneiss_target_compile_warnings(gneiss_editor_gizmo_input_replay_test)
  gneiss_target_output_directories(gneiss_editor_gizmo_input_replay_test)
  set_target_properties(gneiss_editor_gizmo_input_replay_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.gizmo-input-replay COMMAND gneiss_editor_gizmo_input_replay_test)
  add_executable(gneiss_editor_transform_gizmo_drag_test "${PROJECT_SOURCE_DIR}/tests/editor/transform_gizmo_drag_test.cpp")
  target_link_libraries(gneiss_editor_transform_gizmo_drag_test PRIVATE gneiss_scene_document)
  target_compile_definitions(gneiss_editor_transform_gizmo_drag_test PRIVATE
    GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets")
  gneiss_target_compile_warnings(gneiss_editor_transform_gizmo_drag_test)
  gneiss_target_output_directories(gneiss_editor_transform_gizmo_drag_test)
  set_target_properties(gneiss_editor_transform_gizmo_drag_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.transform-gizmo-drag COMMAND gneiss_editor_transform_gizmo_drag_test)
  add_executable(gneiss_editor_ui_layout_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_ui_layout_test.cpp")
  target_link_libraries(gneiss_editor_ui_layout_test PRIVATE gneiss_editor_ui)
  gneiss_target_compile_warnings(gneiss_editor_ui_layout_test)
  gneiss_target_output_directories(gneiss_editor_ui_layout_test)
  set_target_properties(gneiss_editor_ui_layout_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.ui-layout COMMAND gneiss_editor_ui_layout_test)
  add_executable(gneiss_editor_session_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_session_test.cpp")
  target_link_libraries(gneiss_editor_session_test PRIVATE gneiss_scene_document)
  target_compile_definitions(
    gneiss_editor_session_test
    PRIVATE GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
  )
  gneiss_target_compile_warnings(gneiss_editor_session_test)
  gneiss_target_output_directories(gneiss_editor_session_test)
  set_target_properties(gneiss_editor_session_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.session COMMAND gneiss_editor_session_test)
  add_executable(gneiss_editor_project_workspace_test "${PROJECT_SOURCE_DIR}/tests/editor/project_workspace_test.cpp")
  target_link_libraries(gneiss_editor_project_workspace_test PRIVATE gneiss_editor_host)
  target_compile_definitions(
    gneiss_editor_project_workspace_test
    PRIVATE GNEISS_EDITOR_TEST_TEMPLATE="${PROJECT_SOURCE_DIR}/templates/game"
  )
  gneiss_target_compile_warnings(gneiss_editor_project_workspace_test)
  gneiss_target_output_directories(gneiss_editor_project_workspace_test)
  set_target_properties(gneiss_editor_project_workspace_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.project-workspace COMMAND gneiss_editor_project_workspace_test)
  add_executable(gneiss_editor_save_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_save_test.cpp")
  target_link_libraries(gneiss_editor_save_test PRIVATE gneiss_scene_document)
  target_compile_definitions(
    gneiss_editor_save_test
    PRIVATE GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
  )
  gneiss_target_compile_warnings(gneiss_editor_save_test)
  gneiss_target_output_directories(gneiss_editor_save_test)
  set_target_properties(gneiss_editor_save_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.save COMMAND gneiss_editor_save_test)
  add_executable(gneiss_editor_camera_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_camera_test.cpp")
  target_link_libraries(gneiss_editor_camera_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_camera_test)
  gneiss_target_output_directories(gneiss_editor_camera_test)
  set_target_properties(gneiss_editor_camera_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.camera COMMAND gneiss_editor_camera_test)
  add_executable(gneiss_editor_grid_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_grid_test.cpp")
  target_link_libraries(gneiss_editor_grid_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_grid_test)
  gneiss_target_output_directories(gneiss_editor_grid_test)
  set_target_properties(gneiss_editor_grid_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.grid COMMAND gneiss_editor_grid_test)
  add_executable(gneiss_editor_command_history_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_command_history_test.cpp")
  target_link_libraries(gneiss_editor_command_history_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_command_history_test)
  gneiss_target_output_directories(gneiss_editor_command_history_test)
  set_target_properties(gneiss_editor_command_history_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.command-history COMMAND gneiss_editor_command_history_test)
  add_executable(gneiss_editor_author_file_save_test "${PROJECT_SOURCE_DIR}/tests/editor/author_file_save_test.cpp")
  target_link_libraries(gneiss_editor_author_file_save_test PRIVATE gneiss_scene_document)
  gneiss_target_compile_warnings(gneiss_editor_author_file_save_test)
  gneiss_target_output_directories(gneiss_editor_author_file_save_test)
  set_target_properties(gneiss_editor_author_file_save_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.author-file-save COMMAND gneiss_editor_author_file_save_test)
  add_executable(gneiss_editor_prefab_authoring_test "${PROJECT_SOURCE_DIR}/tests/editor/prefab_authoring_test.cpp")
  target_link_libraries(
    gneiss_editor_prefab_authoring_test PRIVATE gneiss_scene_document gneiss_yyjson
  )
  gneiss_target_compile_warnings(gneiss_editor_prefab_authoring_test)
  gneiss_target_output_directories(gneiss_editor_prefab_authoring_test)
  set_target_properties(gneiss_editor_prefab_authoring_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.prefab-authoring COMMAND gneiss_editor_prefab_authoring_test)
  add_executable(gneiss_editor_transform_gizmo_math_test "${PROJECT_SOURCE_DIR}/tests/editor/transform_gizmo_math_test.cpp")
  target_link_libraries(gneiss_editor_transform_gizmo_math_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_transform_gizmo_math_test)
  gneiss_target_output_directories(gneiss_editor_transform_gizmo_math_test)
  set_target_properties(gneiss_editor_transform_gizmo_math_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.transform-gizmo-math COMMAND gneiss_editor_transform_gizmo_math_test)
  add_executable(gneiss_editor_rotation_math_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_rotation_math_test.cpp")
  target_link_libraries(gneiss_editor_rotation_math_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_rotation_math_test)
  gneiss_target_output_directories(gneiss_editor_rotation_math_test)
  set_target_properties(gneiss_editor_rotation_math_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.rotation-math COMMAND gneiss_editor_rotation_math_test)
  add_executable(gneiss_editor_runtime_launch_test "${PROJECT_SOURCE_DIR}/tests/editor/runtime_launch_test.cpp")
  target_link_libraries(gneiss_editor_runtime_launch_test PRIVATE gneiss_editor_host)
  gneiss_target_compile_warnings(gneiss_editor_runtime_launch_test)
  gneiss_target_output_directories(gneiss_editor_runtime_launch_test)
  set_target_properties(gneiss_editor_runtime_launch_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.runtime-launch COMMAND gneiss_editor_runtime_launch_test)
  add_executable(gneiss_editor_runtime_scene_mirror_test "${PROJECT_SOURCE_DIR}/tests/editor/runtime_scene_mirror_test.cpp")
  target_link_libraries(gneiss_editor_runtime_scene_mirror_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_runtime_scene_mirror_test)
  gneiss_target_output_directories(gneiss_editor_runtime_scene_mirror_test)
  set_target_properties(gneiss_editor_runtime_scene_mirror_test PROPERTIES FOLDER "Tests")
  add_test(
    NAME gneiss.editor.runtime-scene-mirror COMMAND gneiss_editor_runtime_scene_mirror_test
  )
  add_executable(gneiss_editor_runtime_property_edits_test "${PROJECT_SOURCE_DIR}/tests/editor/runtime_property_edits_test.cpp")
  target_link_libraries(gneiss_editor_runtime_property_edits_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_runtime_property_edits_test)
  gneiss_target_output_directories(gneiss_editor_runtime_property_edits_test)
  set_target_properties(gneiss_editor_runtime_property_edits_test PROPERTIES FOLDER "Tests")
  add_test(
    NAME gneiss.editor.runtime-property-edits COMMAND gneiss_editor_runtime_property_edits_test
  )
  add_executable(gneiss_editor_ipc_event_test "${PROJECT_SOURCE_DIR}/tests/editor/editor_ipc_event_test.cpp")
  target_link_libraries(gneiss_editor_ipc_event_test PRIVATE gneiss_editor_host)
  gneiss_target_compile_warnings(gneiss_editor_ipc_event_test)
  gneiss_target_output_directories(gneiss_editor_ipc_event_test)
  set_target_properties(gneiss_editor_ipc_event_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.ipc-event COMMAND gneiss_editor_ipc_event_test)
  add_executable(gneiss_editor_console_model_test "${PROJECT_SOURCE_DIR}/tests/editor/console_model_test.cpp")
  target_link_libraries(gneiss_editor_console_model_test PRIVATE gneiss_editor_model)
  target_include_directories(gneiss_editor_console_model_test PRIVATE "${PROJECT_SOURCE_DIR}/editor")
  gneiss_target_compile_warnings(gneiss_editor_console_model_test)
  gneiss_target_output_directories(gneiss_editor_console_model_test)
  set_target_properties(gneiss_editor_console_model_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.editor.console-model COMMAND gneiss_editor_console_model_test)
  if((WIN32 OR UNIX) AND TARGET gneiss_runtime)
    add_executable(gneiss_child_process_fixture "${PROJECT_SOURCE_DIR}/tests/editor/child_process_fixture.cpp")
    target_compile_features(gneiss_child_process_fixture PRIVATE cxx_std_20)
    set_target_properties(gneiss_child_process_fixture PROPERTIES FOLDER "Tests")
    gneiss_target_compile_warnings(gneiss_child_process_fixture)
    gneiss_target_output_directories(gneiss_child_process_fixture)
    add_executable(gneiss_child_process_test "${PROJECT_SOURCE_DIR}/tests/editor/child_process_test.cpp")
    target_link_libraries(gneiss_child_process_test PRIVATE gneiss_child_process)
    target_compile_definitions(
      gneiss_child_process_test
      PRIVATE
        GNEISS_TEST_CHILD_PROCESS="$<TARGET_FILE:gneiss_child_process_fixture>"
    )
    gneiss_target_compile_warnings(gneiss_child_process_test)
    gneiss_target_output_directories(gneiss_child_process_test)
    add_dependencies(gneiss_child_process_test gneiss_child_process_fixture)
    set_target_properties(gneiss_child_process_test PROPERTIES FOLDER "Tests")
    add_test(NAME gneiss.process.child-process COMMAND gneiss_child_process_test)
    set_tests_properties(gneiss.process.child-process PROPERTIES TIMEOUT 15)
    add_executable(gneiss_editor_runtime_stop_failure_test "${PROJECT_SOURCE_DIR}/tests/editor/runtime_stop_failure_test.cpp")
    target_link_libraries(gneiss_editor_runtime_stop_failure_test PRIVATE gneiss_editor_host)
    target_compile_definitions(gneiss_editor_runtime_stop_failure_test PRIVATE GNEISS_TEST_CHILD_PROCESS="$<TARGET_FILE:gneiss_child_process_fixture>")
    add_dependencies(gneiss_editor_runtime_stop_failure_test gneiss_child_process_fixture)
    gneiss_target_compile_warnings(gneiss_editor_runtime_stop_failure_test)
    gneiss_target_output_directories(gneiss_editor_runtime_stop_failure_test)
    set_target_properties(gneiss_editor_runtime_stop_failure_test PROPERTIES FOLDER "Tests")
    add_test(NAME gneiss.editor.runtime-stop-failure COMMAND gneiss_editor_runtime_stop_failure_test)
    set_tests_properties(gneiss.editor.runtime-stop-failure PROPERTIES TIMEOUT 10)

  add_executable(gneiss_editor_runtime_process_test "${PROJECT_SOURCE_DIR}/tests/editor/runtime_process_test.cpp")
    target_link_libraries(gneiss_editor_runtime_process_test PRIVATE gneiss_editor_host)
    target_compile_definitions(
      gneiss_editor_runtime_process_test
      PRIVATE
        GNEISS_TEST_RUNTIME="$<TARGET_FILE:gneiss_runtime>"
        GNEISS_TEST_CHILD_PROCESS="$<TARGET_FILE:gneiss_child_process_fixture>"
        GNEISS_TEST_PROJECT_ROOT="${PROJECT_SOURCE_DIR}/examples/editor_demo"
    )
    gneiss_target_compile_warnings(gneiss_editor_runtime_process_test)
    gneiss_target_output_directories(gneiss_editor_runtime_process_test)
    add_dependencies(
      gneiss_editor_runtime_process_test gneiss_runtime gneiss_child_process_fixture
    )
    set_target_properties(gneiss_editor_runtime_process_test PROPERTIES FOLDER "Tests")
    add_test(NAME gneiss.editor.runtime-process COMMAND gneiss_editor_runtime_process_test)
    set_tests_properties(gneiss.editor.runtime-process PROPERTIES TIMEOUT 15)
    add_executable(gneiss_editor_runtime_scene_loading_test "${PROJECT_SOURCE_DIR}/tests/editor/runtime_scene_loading_test.cpp")





    target_link_libraries(gneiss_editor_runtime_scene_loading_test PRIVATE gneiss_editor_host)

    gneiss_target_compile_warnings(gneiss_editor_runtime_scene_loading_test)
    gneiss_target_output_directories(gneiss_editor_runtime_scene_loading_test)

    set_target_properties(gneiss_editor_runtime_scene_loading_test PROPERTIES FOLDER "Tests")
    add_test(NAME gneiss.editor.runtime-scene-loading COMMAND gneiss_editor_runtime_scene_loading_test)
    set_tests_properties(gneiss.editor.runtime-scene-loading PROPERTIES TIMEOUT 60)
  endif()
  if(TARGET gneiss_editor_assets)
    add_executable(gneiss_editor_asset_background_worker_test "${PROJECT_SOURCE_DIR}/tests/editor/asset_background_worker_test.cpp")
    target_link_libraries(gneiss_editor_asset_background_worker_test PRIVATE gneiss_editor_assets)
    target_compile_definitions(gneiss_editor_asset_background_worker_test PRIVATE
      GNEISS_TEST_SOURCE="${PROJECT_SOURCE_DIR}/tests/data/gltf/static_triangle.gltf")
    gneiss_target_compile_warnings(gneiss_editor_asset_background_worker_test)
    gneiss_target_output_directories(gneiss_editor_asset_background_worker_test)
    set_target_properties(gneiss_editor_asset_background_worker_test PROPERTIES FOLDER "Tests")
    add_test(NAME gneiss.editor.asset-background-worker COMMAND gneiss_editor_asset_background_worker_test)
    set_tests_properties(gneiss.editor.asset-background-worker PROPERTIES TIMEOUT 45)
    # 手动基准保留输入目录及原始输出，不把机器相关耗时作为 CTest 门槛。
    add_executable(gneiss_editor_task_response_benchmark "${PROJECT_SOURCE_DIR}/tests/editor/task_response_benchmark.cpp")
    target_link_libraries(gneiss_editor_task_response_benchmark PRIVATE gneiss_editor_assets gneiss_editor_host)
    target_compile_definitions(gneiss_editor_task_response_benchmark PRIVATE
      GNEISS_TEST_PREFAB="${PROJECT_SOURCE_DIR}/tests/data/assets/prefabs/test.prefab.json")
    gneiss_target_compile_warnings(gneiss_editor_task_response_benchmark)
    gneiss_target_output_directories(gneiss_editor_task_response_benchmark)
    set_target_properties(gneiss_editor_task_response_benchmark PROPERTIES FOLDER "Tests")
    add_executable(gneiss_editor_asset_response_benchmark "${PROJECT_SOURCE_DIR}/tests/editor/asset_response_benchmark.cpp")
    target_link_libraries(gneiss_editor_asset_response_benchmark PRIVATE gneiss_editor_assets)
    target_compile_definitions(gneiss_editor_asset_response_benchmark PRIVATE
      GNEISS_TEST_SOURCE="${PROJECT_SOURCE_DIR}/tests/data/gltf/static_triangle.gltf")
    gneiss_target_compile_warnings(gneiss_editor_asset_response_benchmark)
    gneiss_target_output_directories(gneiss_editor_asset_response_benchmark)
    set_target_properties(gneiss_editor_asset_response_benchmark PROPERTIES FOLDER "Tests")
    add_executable(gneiss_editor_asset_browser_test "${PROJECT_SOURCE_DIR}/tests/editor/asset_browser_model_test.cpp")
    target_link_libraries(gneiss_editor_asset_browser_test PRIVATE gneiss_editor_assets)
    gneiss_target_compile_warnings(gneiss_editor_asset_browser_test)
    gneiss_target_output_directories(gneiss_editor_asset_browser_test)
    set_target_properties(gneiss_editor_asset_browser_test PROPERTIES FOLDER "Tests")
    add_test(NAME gneiss.editor.asset-browser COMMAND gneiss_editor_asset_browser_test)
    add_executable(gneiss_editor_asset_file_watcher_test "${PROJECT_SOURCE_DIR}/tests/editor/asset_file_watcher_test.cpp")
    target_link_libraries(gneiss_editor_asset_file_watcher_test PRIVATE gneiss_editor_assets)
    gneiss_target_compile_warnings(gneiss_editor_asset_file_watcher_test)
    gneiss_target_output_directories(gneiss_editor_asset_file_watcher_test)
    set_target_properties(gneiss_editor_asset_file_watcher_test PROPERTIES FOLDER "Tests")
    add_test(
      NAME gneiss.editor.asset-file-watcher COMMAND gneiss_editor_asset_file_watcher_test
    )
    add_executable(gneiss_editor_author_asset_service_test "${PROJECT_SOURCE_DIR}/tests/editor/author_asset_service_test.cpp")
    target_link_libraries(gneiss_editor_author_asset_service_test PRIVATE gneiss_editor_assets)
    gneiss_target_compile_warnings(gneiss_editor_author_asset_service_test)
    gneiss_target_output_directories(gneiss_editor_author_asset_service_test)
    set_target_properties(gneiss_editor_author_asset_service_test PROPERTIES FOLDER "Tests")
    add_test(NAME gneiss.editor.author-asset-service COMMAND gneiss_editor_author_asset_service_test)
    set_tests_properties(gneiss.editor.author-asset-service PROPERTIES TIMEOUT 30)
    add_executable(gneiss_editor_author_asset_monitor_test "${PROJECT_SOURCE_DIR}/tests/editor/author_asset_monitor_test.cpp")
    target_link_libraries(gneiss_editor_author_asset_monitor_test PRIVATE gneiss_editor_assets)
    gneiss_target_compile_warnings(gneiss_editor_author_asset_monitor_test)
    gneiss_target_output_directories(gneiss_editor_author_asset_monitor_test)
    set_target_properties(gneiss_editor_author_asset_monitor_test PROPERTIES FOLDER "Tests")
    add_test(
      NAME gneiss.editor.author-asset-monitor COMMAND gneiss_editor_author_asset_monitor_test
    )
    add_executable(gneiss_editor_asset_import_test "${PROJECT_SOURCE_DIR}/tests/editor/asset_import_controller_test.cpp")
    target_link_libraries(gneiss_editor_asset_import_test PRIVATE gneiss_editor_assets)
    target_compile_definitions(
      gneiss_editor_asset_import_test
      PRIVATE GNEISS_EDITOR_TEST_GLTF_ROOT="${PROJECT_SOURCE_DIR}/tests/data/gltf"
    )
    gneiss_target_compile_warnings(gneiss_editor_asset_import_test)
    gneiss_target_output_directories(gneiss_editor_asset_import_test)
    set_target_properties(gneiss_editor_asset_import_test PROPERTIES FOLDER "Tests")
    add_test(NAME gneiss.editor.asset-import COMMAND gneiss_editor_asset_import_test)
    if(TARGET gneiss_runtime)





      add_executable(gneiss_editor_asset_workflow_test "${PROJECT_SOURCE_DIR}/tests/editor/asset_workflow_test.cpp")
      target_link_libraries(gneiss_editor_asset_workflow_test PRIVATE gneiss_editor_assets gneiss_editor_host)


      gneiss_target_compile_warnings(gneiss_editor_asset_workflow_test)
      gneiss_target_output_directories(gneiss_editor_asset_workflow_test)
      set_target_properties(gneiss_editor_asset_workflow_test PROPERTIES FOLDER "Tests")
      add_test(NAME gneiss.editor.asset-workflow COMMAND gneiss_editor_asset_workflow_test)
      set_tests_properties(gneiss.editor.asset-workflow PROPERTIES TIMEOUT 60)
    endif()
    add_executable(gneiss_editor_asset_reimport_queue_test "${PROJECT_SOURCE_DIR}/tests/editor/asset_reimport_queue_test.cpp")
    target_link_libraries(gneiss_editor_asset_reimport_queue_test PRIVATE gneiss_editor_assets gneiss_fatal)
    target_compile_definitions(
      gneiss_editor_asset_reimport_queue_test
      PRIVATE GNEISS_EDITOR_TEST_GLTF_ROOT="${PROJECT_SOURCE_DIR}/tests/data/gltf"
    )
    gneiss_target_compile_warnings(gneiss_editor_asset_reimport_queue_test)
    gneiss_target_output_directories(gneiss_editor_asset_reimport_queue_test)
    set_target_properties(gneiss_editor_asset_reimport_queue_test PROPERTIES FOLDER "Tests")
    add_test(
      NAME gneiss.editor.asset-reimport-queue COMMAND gneiss_editor_asset_reimport_queue_test
    )
  endif()
  add_test(
    NAME gneiss.editor.smoke
    COMMAND
      gneiss_editor --smoke --project "${PROJECT_SOURCE_DIR}/examples/editor_demo"
  )
  add_test(NAME gneiss.editor.project-manager-smoke COMMAND gneiss_editor --smoke)
  add_test(
    NAME gneiss.editor.startup-diagnostic
    COMMAND
      "${CMAKE_COMMAND}" -DGNEISS_EDITOR=$<TARGET_FILE:gneiss_editor>
      -DMISSING_PROJECT=${PROJECT_BINARY_DIR}/missing-editor-project -P
      "${PROJECT_SOURCE_DIR}/tests/editor/startup_diagnostic_test.cmake"
  )
  set_tests_properties(gneiss.editor.smoke PROPERTIES
    TIMEOUT 30 FAIL_REGULAR_EXPRESSION "资产监听启动失败"
  )
  set_tests_properties(gneiss.editor.project-manager-smoke PROPERTIES TIMEOUT 30)
  set_tests_properties(gneiss.editor.startup-diagnostic PROPERTIES TIMEOUT 30)
endif()


if(TARGET gneiss_editor_runtime_scene_loading_test)
  target_compile_definitions(gneiss_editor_runtime_scene_loading_test PRIVATE
    GNEISS_TEST_RUNTIME="$<TARGET_FILE:gneiss_runtime>"
    GNEISS_TEST_PROJECT_ROOT="${PROJECT_SOURCE_DIR}/examples/editor_demo")
  add_dependencies(gneiss_editor_runtime_scene_loading_test gneiss_runtime)
endif()
if(TARGET gneiss_editor_asset_workflow_test)
  target_compile_definitions(gneiss_editor_asset_workflow_test PRIVATE
    GNEISS_TEST_TEMPLATE="${PROJECT_SOURCE_DIR}/templates/game"
    GNEISS_TEST_SOURCE="${PROJECT_SOURCE_DIR}/tests/data/gltf/static_triangle.gltf"
    GNEISS_TEST_RUNTIME="$<TARGET_FILE:gneiss_runtime>")
  add_dependencies(gneiss_editor_asset_workflow_test gneiss_runtime)
endif()

# 异步场景测试由宿主驱动任务，不使用生产同步加载入口。
foreach(test IN ITEMS gneiss_editor_prefab_edit_test gneiss_editor_asset_edit_test gneiss_editor_transform_gizmo_drag_test gneiss_editor_session_test gneiss_editor_save_test gneiss_editor_prefab_authoring_test gneiss_editor_runtime_launch_test gneiss_editor_task_response_benchmark gneiss_editor_asset_workflow_test)
  if(TARGET ${test})
    target_link_libraries(${test} PRIVATE gneiss_tasks)
  endif()
endforeach()




