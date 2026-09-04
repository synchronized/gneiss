# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_ipc STATIC
  ipc_dispatcher.cpp
  ipc_dispatcher.h
  ipc_envelope.cpp
  ipc_envelope.h
  ipc_transport.cpp
  ipc_transport.h
)
target_link_libraries(
  gneiss_ipc
  PUBLIC gneiss_uv_runtime gneiss::engine
  PRIVATE gneiss::libuv
)
target_include_directories(gneiss_ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_ipc PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_ipc)
gneiss_target_output_directories(gneiss_ipc)

if(GNEISS_BUILD_TESTING)
  add_executable(gneiss_ipc_envelope_test tests/ipc_envelope_test.cpp)
  target_link_libraries(gneiss_ipc_envelope_test PRIVATE gneiss_ipc)
  gneiss_target_compile_warnings(gneiss_ipc_envelope_test)
  gneiss_target_output_directories(gneiss_ipc_envelope_test)
  add_test(NAME gneiss.ipc.envelope COMMAND gneiss_ipc_envelope_test)

  add_executable(gneiss_ipc_dispatcher_test tests/ipc_dispatcher_test.cpp)
  target_link_libraries(gneiss_ipc_dispatcher_test PRIVATE gneiss_ipc)
  gneiss_target_compile_warnings(gneiss_ipc_dispatcher_test)
  gneiss_target_output_directories(gneiss_ipc_dispatcher_test)
  add_test(NAME gneiss.ipc.dispatcher COMMAND gneiss_ipc_dispatcher_test)

  add_executable(gneiss_ipc_transport_test tests/ipc_transport_test.cpp)
  target_link_libraries(gneiss_ipc_transport_test PRIVATE gneiss_ipc)
  gneiss_target_compile_warnings(gneiss_ipc_transport_test)
  gneiss_target_output_directories(gneiss_ipc_transport_test)
  add_test(NAME gneiss.ipc.transport COMMAND gneiss_ipc_transport_test)
endif()
