// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/protocol/ipc_statistics_protocol.hpp>
#include <gneiss/protocol/registration.hpp>

namespace gneiss::protocol {
error register_types() {
  using statistics = host::ipc_runtime_statistics;
  return meta::registration::type<statistics>()
      .name("gneiss.protocol.runtime_statistics")
      .guid(guid::literal("fc508023-b243-42c5-9ab8-5fd54aa863d5"))
      .property<&statistics::session_id>(meta::property_id{1}, "session_id")(
          serialization::field_policy{.required = true})
      .property<&statistics::sequence>(meta::property_id{2},
                                       "sequence")(serialization::field_policy{.required = true})
      .property<&statistics::frame_index>(meta::property_id{3}, "frame_index")(
          serialization::field_policy{.required = true})
      .property<&statistics::frame_delta_ns>(meta::property_id{4}, "frame_delta_ns")(
          serialization::field_policy{.required = true})
      .property<&statistics::scene_node_count>(meta::property_id{5}, "scene_node_count")(
          serialization::field_policy{.required = true})
      .property<&statistics::entity_count>(meta::property_id{6}, "entity_count")(
          serialization::field_policy{.required = true})
      .property<&statistics::ipc_pending_writes>(meta::property_id{7}, "ipc_pending_writes")(
          serialization::field_policy{.required = true})
      .property<&statistics::ipc_dropped_events>(meta::property_id{8}, "ipc_dropped_events")(
          serialization::field_policy{.required = true})
      .commit();
}
}
