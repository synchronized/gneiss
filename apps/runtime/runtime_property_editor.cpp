// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_property_editor.hpp"
#include "engine/function/world/world_reflection.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace {

gneiss::result to_native_value(const gneiss::ipc_property_value& source,
                               gneiss::engine::core::reflection::property_value& output) noexcept
    try {
  return std::visit(
      [&](const auto& payload) -> gneiss::result {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return gneiss::result::invalid_argument;
        } else if constexpr (std::is_same_v<T, std::array<std::uint8_t, 16>>) {
          output.payload = gneiss::type_id{payload};
        } else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
          output.payload = gneiss::property_vec3{payload[0], payload[1], payload[2]};
        } else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
          output.payload =
              gneiss::property_quaternion{payload[0], payload[1], payload[2], payload[3]};
        } else {
          output.payload = payload;
        }
        return gneiss::result::success;
      },
      source.payload);
} catch (const std::bad_alloc&) {
  return gneiss::result::out_of_memory;
} catch (...) {
  return gneiss::result::internal;
}

gneiss::result from_native_value(const gneiss::engine::core::reflection::property_value& source,
                                 gneiss::ipc_property_value& output) noexcept try {
  return std::visit(
      [&](const auto& payload) -> gneiss::result {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return gneiss::result::unsupported;
        } else if constexpr (std::is_same_v<T, gneiss::type_id>) {
          output.payload = payload.bytes;
        } else if constexpr (std::is_same_v<T, gneiss::property_vec3>) {
          output.payload = std::array{payload.x, payload.y, payload.z};
        } else if constexpr (std::is_same_v<T, gneiss::property_quaternion>) {
          output.payload = std::array{payload.x, payload.y, payload.z, payload.w};
        } else {
          output.payload = payload;
        }
        return gneiss::result::success;
      },
      source.payload);
} catch (const std::bad_alloc&) {
  return gneiss::result::out_of_memory;
} catch (...) {
  return gneiss::result::internal;
}

} // namespace

namespace gneiss::runtime_internal {

runtime_property_editor::~runtime_property_editor() { reset(); }

result runtime_property_editor::initialize(gneiss_world world, runtime_scene_inspection& inspection,
                                           std::uint64_t session_id) noexcept {
  if (world == GNEISS_NULL_WORLD || session_id == 0U || registry_ != nullptr) {
    return result::invalid_argument;
  }
  engine::core::reflection::registry_ref registry;
  const auto operation = engine::world::create_property_registry(registry);
  if (operation.failed()) {
    return operation;
  }
  world_ = world;
  inspection_ = &inspection;
  session_id_ = session_id;
  registry_ = std::move(registry);
  return result::success;
}

void runtime_property_editor::reset() noexcept {
  registry_.reset();
  world_ = GNEISS_NULL_WORLD;
  inspection_ = nullptr;
  session_id_ = 0U;
  revisions_.clear();
}

result runtime_property_editor::execute(const ipc_property_write& command,
                                        ipc_property_write_result& output) noexcept {
  if (!registry_ || inspection_ == nullptr || command.command_id == 0U) {
    return result::invalid_state;
  }
  ipc_property_write_result response{.session_id = session_id_,
                                     .command_id = command.command_id,
                                     .code = GNEISS_SUCCESS,
                                     .revision = 0U,
                                     .message = {},
                                     .canonical_value = {}};
  if (command.session_id != session_id_) {
    response.code = GNEISS_ERROR_INVALID_STATE;
    response.message = "属性写入会话已失效";
    output = std::move(response);
    return result::success;
  }
  gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
  auto operation = inspection_->resolve_entity(command.object, entity);
  if (operation != result::success) {
    response.code = to_native(operation);
    response.message = "运行时对象不存在或已失效";
    output = std::move(response);
    return result::success;
  }

  property_key key{.object = command.object, .field_id = command.field_id};
  std::ranges::copy(command.type_id.bytes, key.type_id.begin());
  auto [revision, inserted] = revisions_.try_emplace(key, 1U);
  (void)inserted;
  response.revision = revision->second;
  if (command.expected_revision != revision->second) {
    response.code = GNEISS_ERROR_INVALID_STATE;
    response.message = "属性修订冲突";
    output = std::move(response);
    return result::success;
  }

  type_id property_type;
  std::ranges::copy(command.type_id.bytes, property_type.bytes.begin());
  const field_id property_field{command.field_id};
  type_info description;
  operation = engine::core::reflection::describe_type(registry_, property_type, description);
  const auto field = std::ranges::find(description.fields, property_field, &field_info::id);
  if (operation.failed() || field == description.fields.end() ||
      !has_flags(field->capabilities, property_capabilities::writable)) {
    response.code =
        field == description.fields.end() ? GNEISS_ERROR_NOT_FOUND : GNEISS_ERROR_UNSUPPORTED;
    if (operation.failed()) {
      response.code = to_native(operation);
    }
    response.message = "属性不存在或不可写";
    output = std::move(response);
    return result::success;
  }
  if (revision->second == std::numeric_limits<std::uint64_t>::max()) {
    response.code = GNEISS_ERROR_OUT_OF_MEMORY;
    response.message = "属性修订号已耗尽";
    output = std::move(response);
    return result::success;
  }
  engine::core::reflection::property_value value;
  operation = to_native_value(command.value, value);
  if (operation.ok()) {
    const property_target target{.context = world_, .object = entity};
    operation = engine::core::reflection::write_property(registry_, property_type, property_field,
                                                         target, value);
    if (operation.ok()) {
      engine::core::reflection::property_value canonical;
      operation = engine::core::reflection::read_property(registry_, property_type, property_field,
                                                          target, canonical);
      if (operation.ok()) {
        operation = from_native_value(canonical, response.canonical_value);
      }
    }
  }
  if (operation.failed()) {
    response.code = to_native(operation);
    response.message = "属性写入被拒绝";
    output = std::move(response);
    return result::success;
  }
  response.revision = ++revision->second;
  response.message = "属性已应用";
  output = std::move(response);
  return result::success;
}

} // namespace gneiss::runtime_internal
