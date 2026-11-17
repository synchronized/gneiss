# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_runtime_ipc STATIC
  ipc/inbound/control_commands.cpp
  ipc/inbound/asset_commands.cpp
  ipc/inbound/scene_commands.cpp
  ipc/inbound/inspection_commands.cpp
  ipc/inbound/property_commands.cpp
  ipc/inbound/session_commands.cpp
  ipc/outbound/heartbeat_response.cpp
  ipc/outbound/asset_events.cpp
  ipc/outbound/inspection_events.cpp
  ipc/outbound/log_events.cpp
  ipc/outbound/property_events.cpp
  ipc/outbound/protocol_error_response.cpp
  ipc/outbound/ready_event.cpp
  ipc/outbound/runtime_ipc_outbound.hpp
  ipc/outbound/session_hello_event.cpp
  ipc/outbound/shutdown_event.cpp
  ipc/outbound/state_event.cpp
  ipc/outbound/statistics_events.cpp
  ipc/runtime_command_context.cpp
  ipc/runtime_command_context.hpp
  ipc/runtime_commands.cpp
  ipc/runtime_commands.hpp
  ipc/runtime_ipc_session.cpp
  ipc/runtime_ipc_session.hpp
  assets/runtime_asset_reloader.cpp
  assets/runtime_asset_reloader.hpp
  inspection/runtime_property_editor.cpp
  inspection/runtime_property_editor.hpp
  inspection/runtime_scene_inspection.cpp
  inspection/runtime_scene_inspection.hpp
  scene/runtime_scene_loader.cpp
  scene/runtime_scene_loader.hpp
)
target_link_libraries(
  gneiss_runtime_ipc PUBLIC gneiss_protocol PRIVATE gneiss_project_config gneiss::engine
)
target_include_directories(gneiss_runtime_ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_features(gneiss_runtime_ipc PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_runtime_ipc)
gneiss_target_output_directories(gneiss_runtime_ipc)

add_executable(gneiss_runtime "${PROJECT_SOURCE_DIR}/runtime/main.cpp" application.cpp application.hpp runtime_logging.cpp runtime_logging.hpp)
target_link_libraries(
  gneiss_runtime
  PRIVATE gneiss_file_log_sink gneiss_project_config gneiss::engine gneiss_runtime_ipc gneiss_tasks
)

target_compile_features(gneiss_runtime PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_runtime)
gneiss_target_output_directories(gneiss_runtime)


target_link_libraries(gneiss_runtime PRIVATE gneiss_core)
