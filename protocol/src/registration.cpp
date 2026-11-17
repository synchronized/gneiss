// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/protocol/ipc_asset_protocol.hpp>
#include <gneiss/protocol/ipc_control_protocol.hpp>
#include <gneiss/protocol/ipc_scene_protocol.hpp>
#include <gneiss/protocol/ipc_statistics_protocol.hpp>
#include <gneiss/protocol/registration.hpp>

namespace gneiss::protocol {
error register_types() {
  using statistics = host::ipc_runtime_statistics;
  using state = host::ipc_control_state;
  auto status = meta::registration::enumeration<state>()
                    .name("gneiss.protocol.control_state")
                    .guid(guid::literal("6fa0d3eb-b56e-4bb5-aea6-779e43ad3cba"))
                    .value("loading", state::loading)
                    .value("ready", state::ready)
                    .value("running", state::running)
                    .value("paused", state::paused)
                    .value("stopping", state::stopping)
                    .commit();
  if (status.failed())
    return status;
  using message = host::ipc_control_state_message;
  status = meta::registration::type<message>()
               .name("gneiss.protocol.control_state_message")
               .guid(guid::literal("f731345f-7eb2-4fb6-9b74-165754178031"))
               .property<&message::state>(meta::property_id{1},
                                          "state")(serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  using phase = host::ipc_scene_phase;
  status = meta::registration::enumeration<phase>()
               .name("gneiss.protocol.scene_phase")
               .guid(guid::literal("b59fd712-d6f1-488a-93ea-c3b984a14ff8"))
               .value("preparing", phase::preparing)
               .value("assets", phase::assets)
               .value("instantiating", phase::instantiating)
               .value("ready", phase::ready)
               .value("applied", phase::applied)
               .value("failed", phase::failed)
               .value("cancelled", phase::cancelled)
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<host::ipc_scene_request>()
               .property<&host::ipc_scene_request::session>(meta::property_id{1}, "session")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_scene_request::revision>(meta::property_id{2}, "revision")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_scene_request::uri>(meta::property_id{3}, "uri")(
                   serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<host::ipc_scene_budget>()
               .property<&host::ipc_scene_budget::candidate_logical_bytes>(
                   meta::property_id{1},
                   "candidate_logical_bytes")(serialization::field_policy{.required = true})
               .property<&host::ipc_scene_budget::candidate_cpu_data_bytes>(
                   meta::property_id{2},
                   "candidate_cpu_data_bytes")(serialization::field_policy{.required = true})
               .property<&host::ipc_scene_budget::application_logical_bytes>(
                   meta::property_id{3},
                   "application_logical_bytes")(serialization::field_policy{.required = true})
               .property<&host::ipc_scene_budget::application_cpu_data_bytes>(
                   meta::property_id{4},
                   "application_cpu_data_bytes")(serialization::field_policy{.required = true})
               .property<&host::ipc_scene_budget::available_bytes>(meta::property_id{5},
                                                                   "available_bytes")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_scene_budget::cleanup_complete>(meta::property_id{6},
                                                                    "cleanup_complete")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_scene_budget::cleanup_pending>(meta::property_id{7},
                                                                   "cleanup_pending")(
                   serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<host::ipc_scene_progress>()
               .property<&host::ipc_scene_progress::source>(meta::property_id{1}, "source")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_scene_progress::phase>(meta::property_id{2}, "phase")(
                   serialization::field_policy{.required = true, .enum_name = true})
               .property<&host::ipc_scene_progress::completed>(meta::property_id{3}, "completed")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_scene_progress::total>(meta::property_id{4}, "total")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_scene_progress::can_cancel>(meta::property_id{5}, "can_cancel")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_scene_progress::message>(meta::property_id{6}, "message")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_scene_progress::budget>(meta::property_id{7}, "budget")(
                   serialization::field_policy{.required = false})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::enumeration<host::ipc_asset_type>()
               .name("gneiss.protocol.ipc_asset_type")
               .guid(guid::literal("a4af4397-04d0-4c88-8d9a-c2033e05fa36"))
               .value("texture", host::ipc_asset_type::texture)
               .value("material", host::ipc_asset_type::material)
               .value("static_mesh", host::ipc_asset_type::static_mesh)
               .value("scene", host::ipc_asset_type::scene)
               .value("prefab", host::ipc_asset_type::prefab)
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::enumeration<host::ipc_asset_apply_status>()
               .name("gneiss.protocol.ipc_asset_apply_status")
               .guid(guid::literal("06910b77-0bab-4db5-80d4-bf787c425029"))
               .value("applied", host::ipc_asset_apply_status::applied)
               .value("failed", host::ipc_asset_apply_status::failed)
               .value("stale", host::ipc_asset_apply_status::stale)
               .value("restart_required", host::ipc_asset_apply_status::restart_required)
               .value("preparing", host::ipc_asset_apply_status::preparing)
               .value("uploading", host::ipc_asset_apply_status::uploading)
               .value("cancelled", host::ipc_asset_apply_status::cancelled)
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<host::ipc_asset_revision>()
               .property<&host::ipc_asset_revision::uri>(meta::property_id{1}, "uri")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_asset_revision::type>(meta::property_id{2}, "type")(
                   serialization::field_policy{.required = true, .enum_name = true})
               .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<host::ipc_asset_reload_request>()
          .property<&host::ipc_asset_reload_request::session_id>(
              meta::property_id{1}, "session_id")(serialization::field_policy{.required = true})
          .property<&host::ipc_asset_reload_request::revision>(meta::property_id{2}, "revision")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_asset_reload_request::assets>(meta::property_id{3}, "assets")(
              serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<host::ipc_asset_cancel_request>()
          .property<&host::ipc_asset_cancel_request::session_id>(
              meta::property_id{1}, "session_id")(serialization::field_policy{.required = true})
          .property<&host::ipc_asset_cancel_request::revision>(meta::property_id{2}, "revision")(
              serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<host::ipc_asset_item_result>()
               .property<&host::ipc_asset_item_result::asset>(meta::property_id{1}, "asset")(
                   serialization::field_policy{.required = true})
               .property<[](const host::ipc_asset_item_result& v) { return v.operation.code(); },
                         [](host::ipc_asset_item_result& v, std::int32_t code) {
                           v.operation = error{code};
                         }>(meta::property_id{2},
                            "operation")(serialization::field_policy{.required = true})
               .property<&host::ipc_asset_item_result::applied>(meta::property_id{3}, "applied")(
                   serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<host::ipc_asset_reload_result>()
          .property<&host::ipc_asset_reload_result::session_id>(meta::property_id{1}, "session_id")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_asset_reload_result::revision>(meta::property_id{2}, "revision")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_asset_reload_result::status>(meta::property_id{3}, "status")(
              serialization::field_policy{.required = true, .enum_name = true})
          .property<&host::ipc_asset_reload_result::message>(meta::property_id{4}, "message")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_asset_reload_result::can_cancel>(meta::property_id{5}, "can_cancel")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_asset_reload_result::completed_assets>(meta::property_id{6},
                                                                      "completed_assets")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_asset_reload_result::total_assets>(
              meta::property_id{7}, "total_assets")(serialization::field_policy{.required = true})
          .property<&host::ipc_asset_reload_result::items>(meta::property_id{8}, "items")(
              serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
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
