# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_editor_host STATIC
  runtime_log_adapter.hpp
  editor_project.hpp
  ipc/editor_ipc_event.hpp
  ipc/editor_ipc_router.cpp
  ipc/editor_ipc_router.hpp
  ipc/editor_ipc_session.cpp
  ipc/editor_ipc_session.hpp
  ipc/inbound/asset_events.cpp
  ipc/inbound/scene_events.cpp
  ipc/inbound/control_events.cpp
  ipc/inbound/event_decode.hpp
  ipc/inbound/inspection_events.cpp
  ipc/inbound/log_events.cpp
  ipc/inbound/property_events.cpp
  ipc/inbound/session_events.cpp
  ipc/inbound/statistics_events.cpp
  ipc/outbound/editor_ipc_outbound.hpp
  ipc/outbound/asset_reload_command.cpp
  ipc/outbound/heartbeat_command.cpp
  ipc/outbound/inspection_resync_command.cpp
  ipc/outbound/pause_command.cpp
  ipc/outbound/property_write_command.cpp
  ipc/outbound/resume_command.cpp
  ipc/outbound/stop_command.cpp
  package_archive.cpp
  package_archive.hpp
  project_workspace.cpp
  project_workspace.hpp
  runtime_process.cpp
  runtime_process.hpp
  runtime_property_adapter.hpp
  runtime_scene_adapter.hpp
)
target_link_libraries(
  gneiss_editor_host
  PRIVATE gneiss_yyjson gneiss_child_process
  PUBLIC gneiss::engine gneiss_scene_document gneiss_app_project gneiss_app_ipc_protocol
)
target_include_directories(
  gneiss_editor_host
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/ipc"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ipc/inbound"
          "${CMAKE_CURRENT_SOURCE_DIR}/ipc/outbound"
)
target_link_libraries(gneiss_editor_host PUBLIC gneiss_native_interface)
target_compile_features(gneiss_editor_host PUBLIC cxx_std_20)
target_compile_definitions(
  gneiss_editor_host
  PRIVATE GNEISS_EDITOR_GAME_TEMPLATE_DIR="${PROJECT_SOURCE_DIR}/templates/game"
)
gneiss_target_compile_warnings(gneiss_editor_host)
gneiss_target_output_directories(gneiss_editor_host)

if(TARGET gneiss_asset_import_sdk)
  target_link_libraries(gneiss_editor_host PRIVATE gneiss_asset_import_sdk)
endif()

add_executable(
  gneiss_editor
  "${PROJECT_SOURCE_DIR}/apps/editor/main.cpp"
  editor_application.cpp
  editor_application.hpp
  native_dialog.cpp
  native_dialog.hpp
  project_manager.cpp
  project_manager.hpp
)
target_link_libraries(
  gneiss_editor
  PRIVATE gneiss_editor_host gneiss_editor_ui gneiss_child_process gneiss_tasks
)
if((WIN32 OR UNIX) AND TARGET gneiss_runtime)
  target_compile_definitions(
    gneiss_editor PRIVATE GNEISS_EDITOR_HAS_RUNTIME=1
                          GNEISS_EDITOR_RUNTIME_PATH="$<TARGET_FILE:gneiss_runtime>"
                          GNEISS_EDITOR_PROJECT_PATH="$<TARGET_FILE:gneiss_project>"
                          GNEISS_EDITOR_CMAKE_PATH="${CMAKE_COMMAND}"
  )
  add_dependencies(gneiss_editor gneiss_runtime)
endif()
if(TARGET gneiss_editor_assets)
  target_link_libraries(gneiss_editor PRIVATE gneiss_editor_assets)
  target_compile_definitions(gneiss_editor PRIVATE GNEISS_EDITOR_HAS_ASSET_BROWSER=1)
endif()
if(WIN32)
  target_link_libraries(gneiss_editor PRIVATE ole32 shell32 uuid)
endif()
gneiss_target_compile_warnings(gneiss_editor)
gneiss_target_output_directories(gneiss_editor)

add_executable(gneiss_project "${PROJECT_SOURCE_DIR}/apps/project/main.cpp" project_cli.cpp project_cli.hpp)
target_link_libraries(gneiss_project PRIVATE gneiss_editor_host gneiss_child_process)
target_compile_definitions(
  gneiss_project PRIVATE GNEISS_EDITOR_CMAKE_PATH="${CMAKE_COMMAND}"
)
target_compile_definitions(
  gneiss_editor PRIVATE GNEISS_EDITOR_PROJECT_PATH="$<TARGET_FILE:gneiss_project>"
)
add_dependencies(gneiss_editor gneiss_project)
gneiss_target_compile_warnings(gneiss_project)
gneiss_target_output_directories(gneiss_project)

install(TARGETS gneiss_editor gneiss_project RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  add_executable(gneiss_editor_scene_loader_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_scene_loader_test.cpp")
  add_executable(gneiss_editor_prefab_edit_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_prefab_edit_test.cpp")
  target_link_libraries(gneiss_editor_prefab_edit_test PRIVATE gneiss_scene_document gneiss_tasks)
  gneiss_target_compile_warnings(gneiss_editor_prefab_edit_test)
  gneiss_target_output_directories(gneiss_editor_prefab_edit_test)
  add_test(NAME gneiss.editor.prefab-edit COMMAND gneiss_editor_prefab_edit_test)
  set_tests_properties(gneiss.editor.prefab-edit PROPERTIES TIMEOUT 45)
  add_executable(gneiss_editor_asset_edit_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_asset_edit_test.cpp")
  target_link_libraries(gneiss_editor_asset_edit_test PRIVATE gneiss_scene_document gneiss_tasks)
  gneiss_target_compile_warnings(gneiss_editor_asset_edit_test)
  gneiss_target_output_directories(gneiss_editor_asset_edit_test)
  add_test(NAME gneiss.editor.asset-edit COMMAND gneiss_editor_asset_edit_test)
  target_link_libraries(gneiss_editor_scene_loader_test PRIVATE gneiss_scene_document gneiss_tasks)
  target_compile_definitions(gneiss_editor_scene_loader_test PRIVATE
    GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets")
  gneiss_target_compile_warnings(gneiss_editor_scene_loader_test)
  gneiss_target_output_directories(gneiss_editor_scene_loader_test)
  add_test(NAME gneiss.editor.scene-loader COMMAND gneiss_editor_scene_loader_test)
  if(WIN32)
    add_executable(gneiss_editor_native_window_interaction_test "${PROJECT_SOURCE_DIR}/tests/host/editor/native_window_interaction_test.cpp")
    target_link_libraries(gneiss_editor_native_window_interaction_test PRIVATE gneiss_editor_ui user32)
    if(TARGET gneiss_editor_assets)
      target_link_libraries(gneiss_editor_native_window_interaction_test PRIVATE gneiss_editor_assets gneiss_editor_host)
      target_compile_definitions(gneiss_editor_native_window_interaction_test PRIVATE GNEISS_TEST_BACKGROUND_ASSETS)
    endif()
    gneiss_target_compile_warnings(gneiss_editor_native_window_interaction_test)
    gneiss_target_output_directories(gneiss_editor_native_window_interaction_test)
    add_test(NAME gneiss.editor.native-window-interaction COMMAND gneiss_editor_native_window_interaction_test)
    set_tests_properties(gneiss.editor.native-window-interaction PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
  endif()
  add_executable(gneiss_editor_gizmo_input_replay_test "${PROJECT_SOURCE_DIR}/tests/host/editor/gizmo_input_replay_test.cpp")
  target_link_libraries(gneiss_editor_gizmo_input_replay_test PRIVATE gneiss_editor_model gneiss_imguizmo)
  gneiss_target_compile_warnings(gneiss_editor_gizmo_input_replay_test)
  gneiss_target_output_directories(gneiss_editor_gizmo_input_replay_test)
  add_test(NAME gneiss.editor.gizmo-input-replay COMMAND gneiss_editor_gizmo_input_replay_test)
  add_executable(gneiss_editor_transform_gizmo_drag_test "${PROJECT_SOURCE_DIR}/tests/host/editor/transform_gizmo_drag_test.cpp")
  target_link_libraries(gneiss_editor_transform_gizmo_drag_test PRIVATE gneiss_scene_document)
  target_compile_definitions(gneiss_editor_transform_gizmo_drag_test PRIVATE
    GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets")
  gneiss_target_compile_warnings(gneiss_editor_transform_gizmo_drag_test)
  gneiss_target_output_directories(gneiss_editor_transform_gizmo_drag_test)
  add_test(NAME gneiss.editor.transform-gizmo-drag COMMAND gneiss_editor_transform_gizmo_drag_test)
  add_executable(gneiss_editor_ui_layout_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_ui_layout_test.cpp")
  target_link_libraries(gneiss_editor_ui_layout_test PRIVATE gneiss_editor_ui)
  gneiss_target_compile_warnings(gneiss_editor_ui_layout_test)
  gneiss_target_output_directories(gneiss_editor_ui_layout_test)
  add_test(NAME gneiss.editor.ui-layout COMMAND gneiss_editor_ui_layout_test)
  add_executable(gneiss_editor_session_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_session_test.cpp")
  target_link_libraries(gneiss_editor_session_test PRIVATE gneiss_scene_document)
  target_compile_definitions(
    gneiss_editor_session_test
    PRIVATE GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
  )
  gneiss_target_compile_warnings(gneiss_editor_session_test)
  gneiss_target_output_directories(gneiss_editor_session_test)
  add_test(NAME gneiss.editor.session COMMAND gneiss_editor_session_test)
  add_executable(gneiss_editor_project_workspace_test "${PROJECT_SOURCE_DIR}/tests/host/editor/project_workspace_test.cpp")
  target_link_libraries(gneiss_editor_project_workspace_test PRIVATE gneiss_editor_host)
  target_compile_definitions(
    gneiss_editor_project_workspace_test
    PRIVATE GNEISS_EDITOR_TEST_TEMPLATE="${PROJECT_SOURCE_DIR}/templates/game"
  )
  gneiss_target_compile_warnings(gneiss_editor_project_workspace_test)
  gneiss_target_output_directories(gneiss_editor_project_workspace_test)
  add_test(NAME gneiss.editor.project-workspace COMMAND gneiss_editor_project_workspace_test)
  add_executable(gneiss_editor_save_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_save_test.cpp")
  target_link_libraries(gneiss_editor_save_test PRIVATE gneiss_scene_document)
  target_compile_definitions(
    gneiss_editor_save_test
    PRIVATE GNEISS_EDITOR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
  )
  gneiss_target_compile_warnings(gneiss_editor_save_test)
  gneiss_target_output_directories(gneiss_editor_save_test)
  add_test(NAME gneiss.editor.save COMMAND gneiss_editor_save_test)
  add_executable(gneiss_editor_camera_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_camera_test.cpp")
  target_link_libraries(gneiss_editor_camera_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_camera_test)
  gneiss_target_output_directories(gneiss_editor_camera_test)
  add_test(NAME gneiss.editor.camera COMMAND gneiss_editor_camera_test)
  add_executable(gneiss_editor_grid_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_grid_test.cpp")
  target_link_libraries(gneiss_editor_grid_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_grid_test)
  gneiss_target_output_directories(gneiss_editor_grid_test)
  add_test(NAME gneiss.editor.grid COMMAND gneiss_editor_grid_test)
  add_executable(gneiss_editor_command_history_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_command_history_test.cpp")
  target_link_libraries(gneiss_editor_command_history_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_command_history_test)
  gneiss_target_output_directories(gneiss_editor_command_history_test)
  add_test(NAME gneiss.editor.command-history COMMAND gneiss_editor_command_history_test)
  add_executable(gneiss_editor_author_file_save_test "${PROJECT_SOURCE_DIR}/tests/host/editor/author_file_save_test.cpp")
  target_link_libraries(gneiss_editor_author_file_save_test PRIVATE gneiss_scene_document)
  target_include_directories(gneiss_editor_author_file_save_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  gneiss_target_compile_warnings(gneiss_editor_author_file_save_test)
  gneiss_target_output_directories(gneiss_editor_author_file_save_test)
  add_test(NAME gneiss.editor.author-file-save COMMAND gneiss_editor_author_file_save_test)
  add_executable(gneiss_editor_prefab_authoring_test "${PROJECT_SOURCE_DIR}/tests/host/editor/prefab_authoring_test.cpp")
  target_link_libraries(
    gneiss_editor_prefab_authoring_test PRIVATE gneiss_scene_document gneiss_yyjson
  )
  gneiss_target_compile_warnings(gneiss_editor_prefab_authoring_test)
  gneiss_target_output_directories(gneiss_editor_prefab_authoring_test)
  add_test(NAME gneiss.editor.prefab-authoring COMMAND gneiss_editor_prefab_authoring_test)
  add_executable(gneiss_editor_transform_gizmo_math_test "${PROJECT_SOURCE_DIR}/tests/host/editor/transform_gizmo_math_test.cpp")
  target_link_libraries(gneiss_editor_transform_gizmo_math_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_transform_gizmo_math_test)
  gneiss_target_output_directories(gneiss_editor_transform_gizmo_math_test)
  add_test(NAME gneiss.editor.transform-gizmo-math COMMAND gneiss_editor_transform_gizmo_math_test)
  add_executable(gneiss_editor_rotation_math_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_rotation_math_test.cpp")
  target_link_libraries(gneiss_editor_rotation_math_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_rotation_math_test)
  gneiss_target_output_directories(gneiss_editor_rotation_math_test)
  add_test(NAME gneiss.editor.rotation-math COMMAND gneiss_editor_rotation_math_test)
  add_executable(gneiss_editor_runtime_launch_test "${PROJECT_SOURCE_DIR}/tests/host/editor/runtime_launch_test.cpp")
  target_link_libraries(gneiss_editor_runtime_launch_test PRIVATE gneiss_editor_host)
  gneiss_target_compile_warnings(gneiss_editor_runtime_launch_test)
  gneiss_target_output_directories(gneiss_editor_runtime_launch_test)
  add_test(NAME gneiss.editor.runtime-launch COMMAND gneiss_editor_runtime_launch_test)
  add_executable(gneiss_editor_runtime_scene_mirror_test "${PROJECT_SOURCE_DIR}/tests/host/editor/runtime_scene_mirror_test.cpp")
  target_link_libraries(gneiss_editor_runtime_scene_mirror_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_runtime_scene_mirror_test)
  gneiss_target_output_directories(gneiss_editor_runtime_scene_mirror_test)
  add_test(
    NAME gneiss.editor.runtime-scene-mirror COMMAND gneiss_editor_runtime_scene_mirror_test
  )
  add_executable(gneiss_editor_runtime_property_edits_test "${PROJECT_SOURCE_DIR}/tests/host/editor/runtime_property_edits_test.cpp")
  target_link_libraries(gneiss_editor_runtime_property_edits_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_runtime_property_edits_test)
  gneiss_target_output_directories(gneiss_editor_runtime_property_edits_test)
  add_test(
    NAME gneiss.editor.runtime-property-edits COMMAND gneiss_editor_runtime_property_edits_test
  )
  add_executable(gneiss_editor_ipc_event_test "${PROJECT_SOURCE_DIR}/tests/host/editor/editor_ipc_event_test.cpp")
  target_link_libraries(gneiss_editor_ipc_event_test PRIVATE gneiss_editor_host)
  gneiss_target_compile_warnings(gneiss_editor_ipc_event_test)
  gneiss_target_output_directories(gneiss_editor_ipc_event_test)
  add_test(NAME gneiss.editor.ipc-event COMMAND gneiss_editor_ipc_event_test)
  add_executable(gneiss_editor_console_model_test "${PROJECT_SOURCE_DIR}/tests/host/editor/console_model_test.cpp")
  target_link_libraries(gneiss_editor_console_model_test PRIVATE gneiss_editor_model)
  gneiss_target_compile_warnings(gneiss_editor_console_model_test)
  gneiss_target_output_directories(gneiss_editor_console_model_test)
  add_test(NAME gneiss.editor.console-model COMMAND gneiss_editor_console_model_test)
  if((WIN32 OR UNIX) AND TARGET gneiss_runtime)
    add_executable(gneiss_child_process_fixture "${PROJECT_SOURCE_DIR}/tests/host/editor/child_process_fixture.cpp")
    target_compile_features(gneiss_child_process_fixture PRIVATE cxx_std_20)
    gneiss_target_compile_warnings(gneiss_child_process_fixture)
    gneiss_target_output_directories(gneiss_child_process_fixture)
    add_executable(gneiss_child_process_test "${PROJECT_SOURCE_DIR}/tests/host/editor/child_process_test.cpp")
    target_link_libraries(gneiss_child_process_test PRIVATE gneiss_child_process)
    target_compile_definitions(
      gneiss_child_process_test
      PRIVATE
        GNEISS_TEST_CHILD_PROCESS="$<TARGET_FILE:gneiss_child_process_fixture>"
    )
    gneiss_target_compile_warnings(gneiss_child_process_test)
    gneiss_target_output_directories(gneiss_child_process_test)
    add_dependencies(gneiss_child_process_test gneiss_child_process_fixture)
    add_test(NAME gneiss.process.child-process COMMAND gneiss_child_process_test)
    set_tests_properties(gneiss.process.child-process PROPERTIES TIMEOUT 15)
    add_executable(gneiss_editor_runtime_stop_failure_test "${PROJECT_SOURCE_DIR}/tests/host/editor/runtime_stop_failure_test.cpp")
    target_link_libraries(gneiss_editor_runtime_stop_failure_test PRIVATE gneiss_editor_host)
    target_compile_definitions(gneiss_editor_runtime_stop_failure_test PRIVATE GNEISS_TEST_CHILD_PROCESS="$<TARGET_FILE:gneiss_child_process_fixture>")
    add_dependencies(gneiss_editor_runtime_stop_failure_test gneiss_child_process_fixture)
    gneiss_target_compile_warnings(gneiss_editor_runtime_stop_failure_test)
    gneiss_target_output_directories(gneiss_editor_runtime_stop_failure_test)
    add_test(NAME gneiss.editor.runtime-stop-failure COMMAND gneiss_editor_runtime_stop_failure_test)
    set_tests_properties(gneiss.editor.runtime-stop-failure PROPERTIES TIMEOUT 10)

  add_executable(gneiss_editor_runtime_process_test "${PROJECT_SOURCE_DIR}/tests/host/editor/runtime_process_test.cpp")
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
    add_test(NAME gneiss.editor.runtime-process COMMAND gneiss_editor_runtime_process_test)
    set_tests_properties(gneiss.editor.runtime-process PROPERTIES TIMEOUT 15)
    add_executable(gneiss_editor_runtime_scene_loading_test "${PROJECT_SOURCE_DIR}/tests/host/editor/runtime_scene_loading_test.cpp")





    target_link_libraries(gneiss_editor_runtime_scene_loading_test PRIVATE gneiss_editor_host)

    gneiss_target_compile_warnings(gneiss_editor_runtime_scene_loading_test)
    gneiss_target_output_directories(gneiss_editor_runtime_scene_loading_test)

    add_test(NAME gneiss.editor.runtime-scene-loading COMMAND gneiss_editor_runtime_scene_loading_test)
    set_tests_properties(gneiss.editor.runtime-scene-loading PROPERTIES TIMEOUT 60)
  endif()
  if(TARGET gneiss_editor_assets)
    add_executable(gneiss_editor_asset_background_worker_test "${PROJECT_SOURCE_DIR}/tests/host/editor/asset_background_worker_test.cpp")
    target_link_libraries(gneiss_editor_asset_background_worker_test PRIVATE gneiss_editor_assets)
    target_compile_definitions(gneiss_editor_asset_background_worker_test PRIVATE
      GNEISS_TEST_SOURCE="${PROJECT_SOURCE_DIR}/tests/data/gltf/static_triangle.gltf")
    gneiss_target_compile_warnings(gneiss_editor_asset_background_worker_test)
    gneiss_target_output_directories(gneiss_editor_asset_background_worker_test)
    add_test(NAME gneiss.editor.asset-background-worker COMMAND gneiss_editor_asset_background_worker_test)
    set_tests_properties(gneiss.editor.asset-background-worker PROPERTIES TIMEOUT 45)
    # 手动基准保留输入目录及原始输出，不把机器相关耗时作为 CTest 门槛。
    add_executable(gneiss_editor_task_response_benchmark "${PROJECT_SOURCE_DIR}/tests/host/editor/task_response_benchmark.cpp")
    target_link_libraries(gneiss_editor_task_response_benchmark PRIVATE gneiss_editor_assets gneiss_editor_host)
    target_compile_definitions(gneiss_editor_task_response_benchmark PRIVATE
      GNEISS_TEST_PREFAB="${PROJECT_SOURCE_DIR}/tests/data/assets/prefabs/test.prefab.json")
    gneiss_target_compile_warnings(gneiss_editor_task_response_benchmark)
    gneiss_target_output_directories(gneiss_editor_task_response_benchmark)
    add_executable(gneiss_editor_asset_response_benchmark "${PROJECT_SOURCE_DIR}/tests/host/editor/asset_response_benchmark.cpp")
    target_link_libraries(gneiss_editor_asset_response_benchmark PRIVATE gneiss_editor_assets)
    target_compile_definitions(gneiss_editor_asset_response_benchmark PRIVATE
      GNEISS_TEST_SOURCE="${PROJECT_SOURCE_DIR}/tests/data/gltf/static_triangle.gltf")
    gneiss_target_compile_warnings(gneiss_editor_asset_response_benchmark)
    gneiss_target_output_directories(gneiss_editor_asset_response_benchmark)
    add_executable(gneiss_editor_asset_browser_test "${PROJECT_SOURCE_DIR}/tests/host/editor/asset_browser_model_test.cpp")
    target_link_libraries(gneiss_editor_asset_browser_test PRIVATE gneiss_editor_assets)
    gneiss_target_compile_warnings(gneiss_editor_asset_browser_test)
    gneiss_target_output_directories(gneiss_editor_asset_browser_test)
    add_test(NAME gneiss.editor.asset-browser COMMAND gneiss_editor_asset_browser_test)
    add_executable(gneiss_editor_asset_file_watcher_test "${PROJECT_SOURCE_DIR}/tests/host/editor/asset_file_watcher_test.cpp")
    target_link_libraries(gneiss_editor_asset_file_watcher_test PRIVATE gneiss_editor_assets)
    gneiss_target_compile_warnings(gneiss_editor_asset_file_watcher_test)
    gneiss_target_output_directories(gneiss_editor_asset_file_watcher_test)
    add_test(
      NAME gneiss.editor.asset-file-watcher COMMAND gneiss_editor_asset_file_watcher_test
    )
    add_executable(gneiss_editor_author_asset_service_test "${PROJECT_SOURCE_DIR}/tests/host/editor/author_asset_service_test.cpp")
    target_link_libraries(gneiss_editor_author_asset_service_test PRIVATE gneiss_editor_assets)
    target_include_directories(gneiss_editor_author_asset_service_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
    gneiss_target_compile_warnings(gneiss_editor_author_asset_service_test)
    gneiss_target_output_directories(gneiss_editor_author_asset_service_test)
    add_test(NAME gneiss.editor.author-asset-service COMMAND gneiss_editor_author_asset_service_test)
    set_tests_properties(gneiss.editor.author-asset-service PROPERTIES TIMEOUT 30)
    add_executable(gneiss_editor_author_asset_monitor_test "${PROJECT_SOURCE_DIR}/tests/host/editor/author_asset_monitor_test.cpp")
    target_link_libraries(gneiss_editor_author_asset_monitor_test PRIVATE gneiss_editor_assets)
    gneiss_target_compile_warnings(gneiss_editor_author_asset_monitor_test)
    gneiss_target_output_directories(gneiss_editor_author_asset_monitor_test)
    add_test(
      NAME gneiss.editor.author-asset-monitor COMMAND gneiss_editor_author_asset_monitor_test
    )
    add_executable(gneiss_editor_asset_import_test "${PROJECT_SOURCE_DIR}/tests/host/editor/asset_import_controller_test.cpp")
    target_link_libraries(gneiss_editor_asset_import_test PRIVATE gneiss_editor_assets)
    target_compile_definitions(
      gneiss_editor_asset_import_test
      PRIVATE GNEISS_EDITOR_TEST_GLTF_ROOT="${PROJECT_SOURCE_DIR}/tests/data/gltf"
    )
    gneiss_target_compile_warnings(gneiss_editor_asset_import_test)
    gneiss_target_output_directories(gneiss_editor_asset_import_test)
    add_test(NAME gneiss.editor.asset-import COMMAND gneiss_editor_asset_import_test)
    if(TARGET gneiss_runtime)





      add_executable(gneiss_editor_asset_workflow_test "${PROJECT_SOURCE_DIR}/tests/host/editor/asset_workflow_test.cpp")
      target_link_libraries(gneiss_editor_asset_workflow_test PRIVATE gneiss_editor_assets gneiss_editor_host)


      gneiss_target_compile_warnings(gneiss_editor_asset_workflow_test)
      gneiss_target_output_directories(gneiss_editor_asset_workflow_test)
      add_test(NAME gneiss.editor.asset-workflow COMMAND gneiss_editor_asset_workflow_test)
      set_tests_properties(gneiss.editor.asset-workflow PROPERTIES TIMEOUT 60)
    endif()
    add_executable(gneiss_editor_asset_reimport_queue_test "${PROJECT_SOURCE_DIR}/tests/host/editor/asset_reimport_queue_test.cpp")
    target_link_libraries(gneiss_editor_asset_reimport_queue_test PRIVATE gneiss_editor_assets)
    target_compile_definitions(
      gneiss_editor_asset_reimport_queue_test
      PRIVATE GNEISS_EDITOR_TEST_GLTF_ROOT="${PROJECT_SOURCE_DIR}/tests/data/gltf"
    )
    gneiss_target_compile_warnings(gneiss_editor_asset_reimport_queue_test)
    gneiss_target_output_directories(gneiss_editor_asset_reimport_queue_test)
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
      "${PROJECT_SOURCE_DIR}/tests/host/editor/startup_diagnostic_test.cmake"
  )
  set_tests_properties(gneiss.editor.smoke PROPERTIES
    TIMEOUT 30 FAIL_REGULAR_EXPRESSION "资产监听启动失败"
  )
  set_tests_properties(gneiss.editor.project-manager-smoke PROPERTIES TIMEOUT 30)
  set_tests_properties(gneiss.editor.startup-diagnostic PROPERTIES TIMEOUT 30)
endif()

# 以下白盒夹具仍验证兼容句柄和内部事务；权限不传回生产宿主。
foreach(test IN ITEMS
    gneiss_editor_scene_loader_test gneiss_editor_prefab_edit_test gneiss_editor_asset_edit_test
    gneiss_editor_session_test gneiss_editor_save_test gneiss_editor_transform_gizmo_drag_test
    gneiss_editor_runtime_launch_test gneiss_editor_task_response_benchmark
    gneiss_editor_asset_workflow_test)
  if(TARGET ${test})
    target_include_directories(${test} PRIVATE "${PROJECT_SOURCE_DIR}/src")
  endif()
endforeach()

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
    target_include_directories(${test} PRIVATE "${PROJECT_SOURCE_DIR}/src")
  endif()
endforeach()
