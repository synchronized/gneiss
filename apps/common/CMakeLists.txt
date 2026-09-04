# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_app_project STATIC
  project_description.cpp
  runtime_log_protocol.cpp
  include/gneiss/app/project_description.h
  include/gneiss/app/runtime_log_protocol.h
)
target_link_libraries(gneiss_app_project PRIVATE gneiss_yyjson PUBLIC gneiss::engine)
target_include_directories(
  gneiss_app_project
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(gneiss_app_project PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_app_project)
gneiss_target_output_directories(gneiss_app_project)

add_library(
  gneiss_app_ipc_protocol STATIC
  ipc/core/ipc_protocol_domains.cpp
  ipc/core/ipc_protocol_domains.h
  ipc/core/ipc_router.h
  ipc/domains/control/ipc_control_protocol.cpp
  ipc/domains/control/ipc_control_protocol.h
  ipc/domains/inspection/ipc_inspection_protocol.cpp
  ipc/domains/inspection/ipc_inspection_protocol.h
  ipc/domains/log/ipc_log_protocol.cpp
  ipc/domains/log/ipc_log_protocol.h
  ipc/domains/property/ipc_property_protocol.cpp
  ipc/domains/property/ipc_property_protocol.h
  ipc/domains/session/ipc_session_protocol.cpp
  ipc/domains/session/ipc_session_protocol.h
  ipc/domains/statistics/ipc_statistics_protocol.cpp
  ipc/domains/statistics/ipc_statistics_protocol.h
)
target_link_libraries(
  gneiss_app_ipc_protocol
  PUBLIC gneiss_ipc gneiss::engine
  PRIVATE gneiss_yyjson
)
target_include_directories(
  gneiss_app_ipc_protocol
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/domains/control"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/domains/inspection"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/domains/log"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/domains/property"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/domains/session"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/domains/statistics"
)
target_compile_features(gneiss_app_ipc_protocol PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_app_ipc_protocol)
gneiss_target_output_directories(gneiss_app_ipc_protocol)

if(BUILD_TESTING)
  add_executable(
    gneiss_app_project_description_test
    tests/project_description_test.cpp
  )
  target_link_libraries(gneiss_app_project_description_test PRIVATE gneiss_app_project)
  target_compile_definitions(
    gneiss_app_project_description_test
    PRIVATE GNEISS_APP_TEST_PROJECT="${PROJECT_SOURCE_DIR}/examples/editor_demo"
  )
  gneiss_target_compile_warnings(gneiss_app_project_description_test)
  gneiss_target_output_directories(gneiss_app_project_description_test)
  add_test(NAME gneiss.app.project-description COMMAND gneiss_app_project_description_test)

  add_executable(
    gneiss_app_runtime_log_protocol_test
    tests/runtime_log_protocol_test.cpp
  )
  target_link_libraries(gneiss_app_runtime_log_protocol_test PRIVATE gneiss_app_project)
  gneiss_target_compile_warnings(gneiss_app_runtime_log_protocol_test)
  gneiss_target_output_directories(gneiss_app_runtime_log_protocol_test)
  add_test(NAME gneiss.app.runtime-log-protocol COMMAND gneiss_app_runtime_log_protocol_test)

  add_executable(gneiss_ipc_router_test tests/ipc/ipc_router_test.cpp)
  target_link_libraries(gneiss_ipc_router_test PRIVATE gneiss_app_ipc_protocol)
  gneiss_target_compile_warnings(gneiss_ipc_router_test)
  gneiss_target_output_directories(gneiss_ipc_router_test)
  add_test(NAME gneiss.app.ipc-router COMMAND gneiss_ipc_router_test)

  add_executable(gneiss_ipc_session_protocol_test tests/ipc/ipc_session_protocol_test.cpp)
  target_link_libraries(gneiss_ipc_session_protocol_test PRIVATE gneiss_app_ipc_protocol)
  gneiss_target_compile_warnings(gneiss_ipc_session_protocol_test)
  gneiss_target_output_directories(gneiss_ipc_session_protocol_test)
  add_test(NAME gneiss.app.ipc-session-protocol COMMAND gneiss_ipc_session_protocol_test)

  add_executable(gneiss_ipc_control_protocol_test tests/ipc/ipc_control_protocol_test.cpp)
  target_link_libraries(gneiss_ipc_control_protocol_test PRIVATE gneiss_app_ipc_protocol)
  gneiss_target_compile_warnings(gneiss_ipc_control_protocol_test)
  gneiss_target_output_directories(gneiss_ipc_control_protocol_test)
  add_test(NAME gneiss.app.ipc-control-protocol COMMAND gneiss_ipc_control_protocol_test)

  add_executable(gneiss_ipc_domain_protocol_test tests/ipc/ipc_domain_protocol_test.cpp)
  target_link_libraries(gneiss_ipc_domain_protocol_test PRIVATE gneiss_app_ipc_protocol)
  gneiss_target_compile_warnings(gneiss_ipc_domain_protocol_test)
  gneiss_target_output_directories(gneiss_ipc_domain_protocol_test)
  add_test(NAME gneiss.app.ipc-domain-protocol COMMAND gneiss_ipc_domain_protocol_test)

  add_executable(gneiss_ipc_protocol_domains_test tests/ipc/ipc_protocol_domains_test.cpp)
  target_link_libraries(gneiss_ipc_protocol_domains_test PRIVATE gneiss_app_ipc_protocol)
  gneiss_target_compile_warnings(gneiss_ipc_protocol_domains_test)
  gneiss_target_output_directories(gneiss_ipc_protocol_domains_test)
  add_test(NAME gneiss.app.ipc-protocol-domains COMMAND gneiss_ipc_protocol_domains_test)

  add_executable(
    gneiss_ipc_property_protocol_test
    tests/ipc/ipc_property_protocol_test.cpp
  )
  target_link_libraries(gneiss_ipc_property_protocol_test PRIVATE gneiss_app_ipc_protocol)
  gneiss_target_compile_warnings(gneiss_ipc_property_protocol_test)
  gneiss_target_output_directories(gneiss_ipc_property_protocol_test)
  add_test(
    NAME gneiss.app.ipc-property-protocol
    COMMAND gneiss_ipc_property_protocol_test
  )
endif()
