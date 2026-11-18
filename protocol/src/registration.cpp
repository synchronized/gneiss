// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/protocol/ipc_asset_protocol.hpp>
#include <gneiss/protocol/ipc_control_protocol.hpp>
#include <gneiss/protocol/ipc_log_protocol.hpp>
#include <gneiss/protocol/ipc_property_protocol.hpp>
#include <gneiss/protocol/ipc_scene_protocol.hpp>
#include <gneiss/protocol/ipc_session_protocol.hpp>
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
  status =
      meta::registration::type<host::ipc_property_write>()
          .name("gneiss.protocol.ipc_property_write")
          .guid(guid::literal("20930a84-d37e-49ad-94bc-f1accaea65ad"))
          .property<&host::ipc_property_write::session_id>(meta::property_id{1}, "session_id")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_property_write::type_id>(meta::property_id{2}, "type_id")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_property_write::field_id>(meta::property_id{3}, "field_id")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_property_write::expected_revision>(meta::property_id{4},
                                                                  "expected_revision")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_property_write::value>(meta::property_id{5}, "value")(
              serialization::field_policy{.required = true})
          .property<[](const host::ipc_property_write& v) { return v.object.value; },
                    [](host::ipc_property_write& v, std::uint64_t n) { v.object.value = n; }>(
              meta::property_id{6}, "object_id")(serialization::field_policy{.required = true})
          .property<[](const host::ipc_property_write& v) { return v.object.generation; },
                    [](host::ipc_property_write& v, std::uint32_t n) { v.object.generation = n; }>(
              meta::property_id{7}, "generation")(serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<host::ipc_property_write_result>()
          .name("gneiss.protocol.ipc_property_write_result")
          .guid(guid::literal("c7efae25-8eda-460b-86bc-400306e20c9f"))
          .property<&host::ipc_property_write_result::session_id>(
              meta::property_id{1}, "session_id")(serialization::field_policy{.required = true})
          .property<&host::ipc_property_write_result::code>(meta::property_id{2}, "code")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_property_write_result::revision>(meta::property_id{3}, "revision")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_property_write_result::message>(meta::property_id{4}, "message")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_property_write_result::canonical_value>(
              meta::property_id{5}, "value")(serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<host::runtime_log_record>()
          .name("gneiss.protocol.runtime_log_record")
          .guid(guid::literal("3e2a92e9-3643-42b7-a0a7-74879e3b4960"))
          .property<&host::runtime_log_record::version>(meta::property_id{1}, "version")(
              serialization::field_policy{.required = true})
          .property<&host::runtime_log_record::severity>(meta::property_id{2}, "severity")(
              serialization::field_policy{.required = true})
          .property<&host::runtime_log_record::sequence>(meta::property_id{3}, "sequence")(
              serialization::field_policy{.required = true})
          .property<&host::runtime_log_record::timestamp_ns>(meta::property_id{4}, "timestamp_ns")(
              serialization::field_policy{.required = true})
          .property<&host::runtime_log_record::thread_id>(meta::property_id{5}, "thread_id")(
              serialization::field_policy{.required = true})
          .property<&host::runtime_log_record::context>(meta::property_id{6}, "context")(
              serialization::field_policy{.required = true})
          .property<&host::runtime_log_record::source>(meta::property_id{7}, "source")(
              serialization::field_policy{.required = true})
          .property<&host::runtime_log_record::category>(meta::property_id{8}, "category")(
              serialization::field_policy{.required = true})
          .property<&host::runtime_log_record::message>(meta::property_id{9}, "message")(
              serialization::field_policy{.required = true})
          .property<[](const host::runtime_log_record& record) { return record.operation.code(); },
                    [](host::runtime_log_record& record, std::int32_t code) {
                      record.operation = error{code};
                    }>(meta::property_id{10},
                       "result")(serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status = meta::registration::enumeration<host::ipc_inspection_change_type>()
               .name("gneiss.protocol.ipc_inspection_change_type")
               .guid(guid::literal("6195049f-08dd-44e8-bc89-4b4e498d28b6"))
               .value("upsert", host::ipc_inspection_change_type::upsert)
               .value("remove", host::ipc_inspection_change_type::remove)
               .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<host::ipc_runtime_object_id>()
          .property<&host::ipc_runtime_object_id::value>(meta::property_id{1}, "value")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_runtime_object_id::generation>(meta::property_id{2}, "generation")(
              serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<host::ipc_inspection_stamp>()
          .property<&host::ipc_inspection_stamp::session_id>(meta::property_id{1}, "session_id")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_stamp::sequence>(meta::property_id{2}, "sequence")(
              serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<host::ipc_inspection_node>()
          .property<&host::ipc_inspection_node::id>(meta::property_id{1}, "id")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_node::parent>(meta::property_id{2}, "parent")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_node::uuid>(meta::property_id{3}, "uuid")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_node::prefab_instance_uuid>(meta::property_id{4},
                                                                      "prefab_instance_uuid")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_node::prefab_source_node_uuid>(meta::property_id{5},
                                                                         "prefab_source_node_uuid")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_node::name>(meta::property_id{6}, "name")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_node::local_transform>(meta::property_id{7},
                                                                 "local_transform")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_node::components>(meta::property_id{8}, "components")(
              serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<host::ipc_inspection_change>()
               .property<&host::ipc_inspection_change::type>(meta::property_id{1}, "type")(
                   serialization::field_policy{.required = true, .enum_name = true})
               .property<&host::ipc_inspection_change::id>(meta::property_id{2}, "id")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_inspection_change::node>(meta::property_id{3}, "node")(
                   serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<host::ipc_inspection_batch>()
          .property<&host::ipc_inspection_batch::stamp>(meta::property_id{1}, "stamp")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_batch::is_full>(meta::property_id{2}, "is_full")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_batch::chunk_index>(meta::property_id{3}, "chunk_index")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_batch::chunk_count>(meta::property_id{4}, "chunk_count")(
              serialization::field_policy{.required = true})
          .property<&host::ipc_inspection_batch::changes>(meta::property_id{5}, "changes")(
              serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<domain_capability>()
               .property<&domain_capability::domain>(meta::property_id{1}, "domain")(
                   serialization::field_policy{.required = true})
               .property<&domain_capability::version>(meta::property_id{2}, "version")(
                   serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<host::ipc_session_hello>()
               .name("gneiss.protocol.ipc_session_hello")
               .guid(guid::literal("a25b43eb-0cd3-4610-bd93-6fa230846421"))
               .property<&host::ipc_session_hello::token>(meta::property_id{1}, "token")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_session_hello::domains>(meta::property_id{2}, "domains")(
                   serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<host::ipc_session_heartbeat>()
               .name("gneiss.protocol.ipc_session_heartbeat")
               .guid(guid::literal("a575680b-2093-4cb6-b88f-0e07b31cff36"))
               .property<&host::ipc_session_heartbeat::nonce>(meta::property_id{1}, "nonce")(
                   serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<host::ipc_session_error>()
               .name("gneiss.protocol.ipc_session_error")
               .guid(guid::literal("a5af8ce9-8988-4b80-8487-bc8b44877317"))
               .property<&host::ipc_session_error::code>(meta::property_id{1}, "code")(
                   serialization::field_policy{.required = true})
               .property<&host::ipc_session_error::message>(meta::property_id{2}, "message")(
                   serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<host::ipc_session_shutdown>()
               .name("gneiss.protocol.ipc_session_shutdown")
               .guid(guid::literal("f489891e-7db1-435a-a54e-8958279501dc"))
               .property<&host::ipc_session_shutdown::exit_code>(meta::property_id{1}, "exit_code")(
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
