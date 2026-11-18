# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(libraries.cmake)

add_library(
  gneiss_editor_host STATIC
  runtime/runtime_log_adapter.hpp
  project/editor_project.hpp
  runtime/ipc/editor_ipc_event.hpp
  runtime/ipc/editor_ipc_router.cpp
  runtime/ipc/editor_ipc_router.hpp
  runtime/ipc/editor_ipc_session.cpp
  runtime/ipc/editor_ipc_session.hpp
  runtime/ipc/inbound/asset_events.cpp
  runtime/ipc/inbound/scene_events.cpp
  runtime/ipc/inbound/control_events.cpp
  runtime/ipc/inbound/inspection_events.cpp
  runtime/ipc/inbound/log_events.cpp
  runtime/ipc/inbound/property_events.cpp
  runtime/ipc/inbound/session_events.cpp
  runtime/ipc/inbound/statistics_events.cpp
  runtime/ipc/outbound/editor_ipc_outbound.hpp
  runtime/ipc/outbound/inspection_resync_command.cpp
  runtime/ipc/outbound/pause_command.cpp
  runtime/ipc/outbound/resume_command.cpp
  runtime/ipc/outbound/stop_command.cpp
  project/package_archive.cpp
  project/package_archive.hpp
  project/project_workspace.cpp
  project/project_workspace.hpp
  runtime/runtime_process.cpp
  runtime/runtime_process.hpp
  runtime/runtime_property_adapter.hpp
  runtime/runtime_scene_adapter.hpp
)
target_link_libraries(
  gneiss_editor_host
  PRIVATE gneiss_yyjson gneiss_child_process
  PUBLIC gneiss::engine gneiss_scene_document gneiss_project_config gneiss_protocol
)
target_include_directories(
  gneiss_editor_host
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ipc"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ipc/inbound"
          "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ipc/outbound"
)
target_compile_features(gneiss_editor_host PUBLIC cxx_std_20)
target_compile_definitions(
  gneiss_editor_host
  PRIVATE GNEISS_EDITOR_GAME_TEMPLATE_DIR="${PROJECT_SOURCE_DIR}/templates/game"
)
gneiss_target_compile_warnings(gneiss_editor_host)
gneiss_target_output_directories(gneiss_editor_host)

if(TARGET gneiss_tooling)
  target_link_libraries(gneiss_editor_host PRIVATE gneiss_tooling)
endif()

add_executable(
  gneiss_editor
  "${PROJECT_SOURCE_DIR}/editor/main.cpp"
  application.cpp
  application.hpp
  ui/dialogs/native_dialog.cpp
  ui/dialogs/native_dialog.hpp
  project/project_manager.cpp
  project/project_manager.hpp
)
target_link_libraries(
  gneiss_editor
  PRIVATE gneiss_logging_sinks gneiss_editor_host gneiss_editor_ui gneiss_child_process gneiss_tasks
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

add_executable(gneiss_project "${PROJECT_SOURCE_DIR}/editor/project/cli/main.cpp" project/cli/project_cli.cpp project/cli/project_cli.hpp)
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


target_link_libraries(gneiss_editor PRIVATE gneiss_core)
target_link_libraries(gneiss_project PRIVATE gneiss_core)
