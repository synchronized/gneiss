# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_ipc_scene_protocol_test "${CMAKE_CURRENT_SOURCE_DIR}/ipc_scene_protocol_test.cpp")
target_link_libraries(gneiss_ipc_scene_protocol_test PRIVATE gneiss_protocol)
gneiss_target_compile_warnings(gneiss_ipc_scene_protocol_test)
gneiss_target_output_directories(gneiss_ipc_scene_protocol_test)
set_target_properties(gneiss_ipc_scene_protocol_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.app.ipc-scene-protocol COMMAND gneiss_ipc_scene_protocol_test)
add_executable(
  gneiss_app_runtime_log_protocol_test
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime_log_protocol_test.cpp"
)
target_link_libraries(gneiss_app_runtime_log_protocol_test PRIVATE gneiss_protocol)
gneiss_target_compile_warnings(gneiss_app_runtime_log_protocol_test)
gneiss_target_output_directories(gneiss_app_runtime_log_protocol_test)
set_target_properties(gneiss_app_runtime_log_protocol_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.app.runtime-log-protocol COMMAND gneiss_app_runtime_log_protocol_test)

add_executable(gneiss_ipc_router_test "${CMAKE_CURRENT_SOURCE_DIR}/ipc_router_test.cpp")
target_link_libraries(gneiss_ipc_router_test PRIVATE gneiss_protocol)
gneiss_target_compile_warnings(gneiss_ipc_router_test)
gneiss_target_output_directories(gneiss_ipc_router_test)
set_target_properties(gneiss_ipc_router_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.app.ipc-router COMMAND gneiss_ipc_router_test)

add_executable(gneiss_ipc_session_protocol_test "${CMAKE_CURRENT_SOURCE_DIR}/ipc_session_protocol_test.cpp")
target_link_libraries(gneiss_ipc_session_protocol_test PRIVATE gneiss_protocol)
gneiss_target_compile_warnings(gneiss_ipc_session_protocol_test)
gneiss_target_output_directories(gneiss_ipc_session_protocol_test)
set_target_properties(gneiss_ipc_session_protocol_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.app.ipc-session-protocol COMMAND gneiss_ipc_session_protocol_test)

add_executable(gneiss_ipc_control_protocol_test "${CMAKE_CURRENT_SOURCE_DIR}/ipc_control_protocol_test.cpp")
target_link_libraries(gneiss_ipc_control_protocol_test PRIVATE gneiss_protocol)
gneiss_target_compile_warnings(gneiss_ipc_control_protocol_test)
gneiss_target_output_directories(gneiss_ipc_control_protocol_test)
set_target_properties(gneiss_ipc_control_protocol_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.app.ipc-control-protocol COMMAND gneiss_ipc_control_protocol_test)

add_executable(gneiss_ipc_domain_protocol_test "${CMAKE_CURRENT_SOURCE_DIR}/ipc_domain_protocol_test.cpp")
target_link_libraries(gneiss_ipc_domain_protocol_test PRIVATE gneiss_protocol)
gneiss_target_compile_warnings(gneiss_ipc_domain_protocol_test)
gneiss_target_output_directories(gneiss_ipc_domain_protocol_test)
set_target_properties(gneiss_ipc_domain_protocol_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.app.ipc-domain-protocol COMMAND gneiss_ipc_domain_protocol_test)

add_executable(gneiss_ipc_protocol_domains_test "${CMAKE_CURRENT_SOURCE_DIR}/ipc_protocol_domains_test.cpp")
target_link_libraries(gneiss_ipc_protocol_domains_test PRIVATE gneiss_protocol)
gneiss_target_compile_warnings(gneiss_ipc_protocol_domains_test)
gneiss_target_output_directories(gneiss_ipc_protocol_domains_test)
set_target_properties(gneiss_ipc_protocol_domains_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.app.ipc-protocol-domains COMMAND gneiss_ipc_protocol_domains_test)

add_executable(
  gneiss_ipc_property_protocol_test
  "${CMAKE_CURRENT_SOURCE_DIR}/ipc_property_protocol_test.cpp"
)
target_link_libraries(gneiss_ipc_property_protocol_test PRIVATE gneiss_protocol)
gneiss_target_compile_warnings(gneiss_ipc_property_protocol_test)
gneiss_target_output_directories(gneiss_ipc_property_protocol_test)
set_target_properties(gneiss_ipc_property_protocol_test PROPERTIES FOLDER "Tests")
add_test(
  NAME gneiss.app.ipc-property-protocol
  COMMAND gneiss_ipc_property_protocol_test
)
add_executable(gneiss_ipc_asset_protocol_test "${CMAKE_CURRENT_SOURCE_DIR}/ipc_asset_protocol_test.cpp")
target_link_libraries(gneiss_ipc_asset_protocol_test PRIVATE gneiss_protocol)
gneiss_target_compile_warnings(gneiss_ipc_asset_protocol_test)
gneiss_target_output_directories(gneiss_ipc_asset_protocol_test)
set_target_properties(gneiss_ipc_asset_protocol_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.app.ipc-asset-protocol COMMAND gneiss_ipc_asset_protocol_test)

  add_executable(gneiss_ipc_dispatcher_test "dispatcher_test.cpp")
  target_link_libraries(gneiss_ipc_dispatcher_test PRIVATE gneiss_protocol)
  gneiss_target_compile_warnings(gneiss_ipc_dispatcher_test)
  gneiss_target_output_directories(gneiss_ipc_dispatcher_test)
  set_target_properties(gneiss_ipc_dispatcher_test PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.ipc.dispatcher COMMAND gneiss_ipc_dispatcher_test)

# 协议测试使用内存负载，不启动 Runtime 或建立真实 IPC 连接。
set_tests_properties(
  gneiss.app.ipc-scene-protocol gneiss.app.runtime-log-protocol gneiss.app.ipc-router
  gneiss.app.ipc-session-protocol gneiss.app.ipc-control-protocol
  gneiss.app.ipc-domain-protocol gneiss.app.ipc-protocol-domains
  gneiss.app.ipc-property-protocol gneiss.app.ipc-asset-protocol
  PROPERTIES LABELS "cpu;integration")
set_tests_properties(gneiss.ipc.dispatcher PROPERTIES LABELS "cpu;unit")
