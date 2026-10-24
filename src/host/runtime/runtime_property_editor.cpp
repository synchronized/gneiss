// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_property_editor.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world_reflection.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace {

gneiss::engine::result to_native_value(const gneiss::host::ipc_property_value& source,
                                       gneiss::engine::property_value& output) noexcept try {
  return std::visit(
      [&](const auto& payload) -> gneiss::engine::result {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return gneiss::engine::result::invalid_argument;
        } else if constexpr (std::is_same_v<T, std::array<std::uint8_t, 16>>) {
          output.payload = gneiss::engine::type_guid{gneiss::engine::guid::from_bytes(payload)};
        } else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
          output.payload = gneiss::property_vec3{payload[0], payload[1], payload[2]};
        } else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
          output.payload =
              gneiss::engine::quaternion_xyzw(payload[0], payload[1], payload[2], payload[3]);
        } else {
          output.payload = payload;
        }
        return gneiss::engine::result::success;
      },
      source.payload);
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result from_native_value(const gneiss::engine::property_value& source,
                                         gneiss::host::ipc_property_value& output) noexcept try {
  return std::visit(
      [&](const auto& payload) -> gneiss::engine::result {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return gneiss::engine::result::unsupported;
        } else if constexpr (std::is_same_v<T, gneiss::engine::type_guid>) {
          output.payload = payload.value.bytes();
        } else if constexpr (std::is_same_v<T, gneiss::property_vec3>) {
          output.payload = std::array{payload.x, payload.y, payload.z};
        } else if constexpr (std::is_same_v<T, gneiss::property_quaternion>) {
          output.payload = std::array{payload.x, payload.y, payload.z, payload.w};
        } else {
          output.payload = payload;
        }
        return gneiss::engine::result::success;
      },
      source.payload);
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

} // namespace

namespace gneiss::host {

runtime_property_editor::~runtime_property_editor() { reset(); }

gneiss::engine::result runtime_property_editor::initialize(gneiss::engine::rid world,
                                                           runtime_scene_inspection& inspection,
                                                           std::uint64_t session_id) noexcept {
  if (world == gneiss::engine::null_rid || session_id == 0U || registry_ != nullptr) {
    return gneiss::engine::result::invalid_argument;
  }
  gneiss::engine::registry_ref registry;
  const auto operation = gneiss::engine::create_property_registry(registry);
  if (operation.failed()) {
    return operation;
  }
  world_ = world;
  inspection_ = &inspection;
  session_id_ = session_id;
  registry_ = std::move(registry);
  return gneiss::engine::result::success;
}

void runtime_property_editor::reset() noexcept {
  registry_.reset();
  world_ = gneiss::engine::null_rid;
  inspection_ = nullptr;
  session_id_ = 0U;
  revisions_.clear();
}

gneiss::engine::result
runtime_property_editor::execute(const ipc_property_write& command,
                                 ipc_property_write_result& output) noexcept {
  if (!registry_ || inspection_ == nullptr || command.command_id == 0U) {
    return gneiss::engine::result::invalid_state;
  }
  ipc_property_write_result response{.session_id = session_id_,
                                     .command_id = command.command_id,
                                     .code = gneiss::engine::result::success.code(),
                                     .revision = 0U,
                                     .message = {},
                                     .canonical_value = {}};
  if (command.session_id != session_id_) {
    response.code = gneiss::engine::result::invalid_state.code();
    response.message = "属性写入会话已失效";
    output = std::move(response);
    return gneiss::engine::result::success;
  }
  std::uint64_t entity = UINT64_C(0);
  auto operation = inspection_->resolve_entity(command.object, entity);
  if (operation != gneiss::engine::result::success) {
    response.code = operation.code();
    response.message = "运行时对象不存在或已失效";
    output = std::move(response);
    return gneiss::engine::result::success;
  }

  property_key key{.object = command.object, .field_id = command.field_id};
  std::ranges::copy(command.type_id.value.bytes(), key.type_id.begin());
  auto [revision, inserted] = revisions_.try_emplace(key, 1U);
  (void)inserted;
  response.revision = revision->second;
  if (command.expected_revision != revision->second) {
    response.code = gneiss::engine::result::invalid_state.code();
    response.message = "属性修订冲突";
    output = std::move(response);
    return gneiss::engine::result::success;
  }

  const gneiss::engine::type_guid property_type{gneiss::engine::guid::from_bytes(key.type_id)};
  const field_id property_field{command.field_id};
  type_info description;
  operation = gneiss::engine::describe_type(registry_, property_type, description);
  const auto field = std::ranges::find(description.fields, property_field, &field_info::id);
  if (operation.failed() || field == description.fields.end() ||
      !has_flags(field->capabilities, property_capabilities::writable)) {
    response.code = field == description.fields.end() ? gneiss::engine::result::not_found.code()
                                                      : gneiss::engine::result::unsupported.code();
    if (operation.failed()) {
      response.code = operation.code();
    }
    response.message = "属性不存在或不可写";
    output = std::move(response);
    return gneiss::engine::result::success;
  }
  if (revision->second == std::numeric_limits<std::uint64_t>::max()) {
    response.code = gneiss::engine::result::out_of_memory.code();
    response.message = "属性修订号已耗尽";
    output = std::move(response);
    return gneiss::engine::result::success;
  }
  gneiss::engine::property_value value;
  operation = to_native_value(command.value, value);
  if (operation.ok()) {
    const property_target target{.context = world_, .object = entity};
    operation =
        gneiss::engine::write_property(registry_, property_type, property_field, target, value);
    if (operation.ok()) {
      gneiss::engine::property_value canonical;
      operation = gneiss::engine::read_property(registry_, property_type, property_field, target,
                                                canonical);
      if (operation.ok()) {
        operation = from_native_value(canonical, response.canonical_value);
      }
    }
  }
  if (operation.failed()) {
    response.code = operation.code();
    response.message = "属性写入被拒绝";
    output = std::move(response);
    return gneiss::engine::result::success;
  }
  response.revision = ++revision->second;
  response.message = "属性已应用";
  output = std::move(response);
  return gneiss::engine::result::success;
}

} // namespace gneiss::host
