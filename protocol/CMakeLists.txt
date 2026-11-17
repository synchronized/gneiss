# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(gneiss_protocol STATIC
  include/gneiss/protocol/message.hpp
  src/registration.cpp
  include/gneiss/protocol/registration.hpp
  include/gneiss/protocol/payload.hpp
  src/dispatcher.cpp
  include/gneiss/protocol/dispatcher.hpp
  include/gneiss/protocol/domains.hpp
  include/gneiss/protocol/ipc_asset_protocol.hpp
  include/gneiss/protocol/ipc_control_protocol.hpp
  include/gneiss/protocol/ipc_inspection_protocol.hpp
  include/gneiss/protocol/ipc_log_protocol.hpp
  include/gneiss/protocol/ipc_property_protocol.hpp
  include/gneiss/protocol/ipc_protocol_domains.hpp
  include/gneiss/protocol/ipc_router.hpp
  include/gneiss/protocol/ipc_scene_protocol.hpp
  include/gneiss/protocol/ipc_session_protocol.hpp
  include/gneiss/protocol/ipc_statistics_protocol.hpp
  include/gneiss/protocol/runtime_log_protocol.hpp
  src/ipc_asset_protocol.cpp
  src/ipc_control_protocol.cpp
  src/ipc_inspection_protocol.cpp
  src/ipc_log_protocol.cpp
  src/ipc_property_protocol.cpp
  src/ipc_protocol_domains.cpp
  src/ipc_scene_protocol.cpp
  src/ipc_session_protocol.cpp
  src/ipc_statistics_protocol.cpp
  src/reflected_json.hpp
  src/runtime_log_protocol.cpp
)
add_library(gneiss::protocol ALIAS gneiss_protocol)
target_include_directories(gneiss_protocol
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(gneiss_protocol PUBLIC gneiss::engine gneiss_ipc PRIVATE gneiss_yyjson)
target_compile_features(gneiss_protocol PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_protocol)
gneiss_target_output_directories(gneiss_protocol)
