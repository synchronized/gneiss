// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "inspection/runtime_property_editor.hpp"
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/transform_properties.hpp>
#include <gneiss/engine/core/property_edit_policy.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/function/scene/world.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace gneiss::host {

runtime_property_editor::~runtime_property_editor() { reset(); }

gneiss::error runtime_property_editor::initialize(gneiss::world& world,
                                                          runtime_scene_inspection& inspection,
                                                          std::uint64_t session_id) {
  if (session_id == 0U || initialized_) {
    return gneiss::error::invalid_argument;
  }
  auto frozen = gneiss::meta::is_frozen();
  if (!frozen)
    return gneiss::error::not_ready;
  world_ = &world;
  inspection_ = &inspection;
  session_id_ = session_id;
  initialized_ = true;
  return gneiss::error::success;
}

void runtime_property_editor::reset() noexcept {
  initialized_ = false;
  world_ = nullptr;
  inspection_ = nullptr;
  session_id_ = 0U;
  revisions_.clear();
}

gneiss::error runtime_property_editor::execute(const ipc_property_write& command,
                                                       ipc_property_write_result& output) {
  if (!initialized_ || inspection_ == nullptr || command.command_id == 0U) {
    return gneiss::error::invalid_state;
  }
  ipc_property_write_result response{.session_id = session_id_,
                                     .command_id = command.command_id,
                                     .code = gneiss::error::success.code(),
                                     .revision = 0U,
                                     .message = {},
                                     .canonical_value = {}};
  if (command.session_id != session_id_) {
    response.code = gneiss::error::invalid_state.code();
    response.message = "属性写入会话已失效";
    output = std::move(response);
    return gneiss::error::success;
  }
  std::uint64_t entity = UINT64_C(0);
  auto operation = inspection_->resolve_entity(command.object, entity);
  if (operation != gneiss::error::success) {
    response.code = operation.code();
    response.message = "运行时对象不存在或已失效";
    output = std::move(response);
    return gneiss::error::success;
  }

  property_key key{.object = command.object, .field_id = command.field_id};
  std::ranges::copy(command.type_id.bytes(), key.type_id.begin());
  auto [revision, inserted] = revisions_.try_emplace(key, 1U);
  (void)inserted;
  response.revision = revision->second;
  if (command.expected_revision != revision->second) {
    response.code = gneiss::error::invalid_state.code();
    response.message = "属性修订冲突";
    output = std::move(response);
    return gneiss::error::success;
  }

  const gneiss::guid property_type{gneiss::guid::from_bytes(key.type_id)};
  const gneiss::meta::property_id property_field{command.field_id};
  auto description = gneiss::meta::find_type(property_type);
  if (!description) {
    response.code = gneiss::error{gneiss::error::not_found}.code();
    response.message = "属性不存在或不可写";
    output = std::move(response);
    return gneiss::error::success;
  }
  auto properties = description.template capability<gneiss::meta::cap::object>().properties();
  if (!description.registered()) {
    response.code = gneiss::error::not_found.code();
    response.message = "属性不存在或不可写";
    output = std::move(response);
    return gneiss::error::success;
  }
  const auto field =
      std::ranges::find(properties, property_field, &gneiss::meta::property::id);
  if (field == properties.end() ||
      !has_flags(field->capabilities(), gneiss::meta::property_capabilities::writable) ||
      gneiss::is_edit_read_only(*field)) {
    response.code = field == properties.end() ? gneiss::error::not_found.code()
                                              : gneiss::error::unsupported.code();
    response.message = "属性不存在或不可写";
    output = std::move(response);
    return gneiss::error::success;
  }
  if (revision->second == std::numeric_limits<std::uint64_t>::max()) {
    response.code = gneiss::error::out_of_memory.code();
    response.message = "属性修订号已耗尽";
    output = std::move(response);
    return gneiss::error::success;
  }
  operation = gneiss::write_component_property(*world_, entity, property_type,
                                                       property_field, command.value);
  if (operation.ok()) {
    auto canonical =
        gneiss::read_component_property(*world_, entity, property_type, property_field);
    if (canonical)
      response.canonical_value = std::move(*canonical);
    else
      operation = canonical.error();
  }
  if (operation.failed()) {
    response.code = operation.code();
    response.message = "属性写入被拒绝";
    output = std::move(response);
    return gneiss::error::success;
  }
  response.revision = ++revision->second;
  response.message = "属性已应用";
  output = std::move(response);
  return gneiss::error::success;
}

} // namespace gneiss::host
