// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_property_editor.h"

#include <algorithm>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace {

gneiss::result to_native_value(const gneiss::ipc_property_value& source,
                               gneiss_property_value& output) noexcept {
  output = GNEISS_PROPERTY_VALUE_INIT;
  return std::visit(
      [&](const auto& payload) -> gneiss::result {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, std::monostate>) {
          return gneiss::result::invalid_argument;
        } else if constexpr (std::is_same_v<Payload, bool>) {
          output.kind = GNEISS_PROPERTY_KIND_BOOL;
          output.payload.bool_value = payload ? UINT8_C(1) : UINT8_C(0);
        } else if constexpr (std::is_same_v<Payload, std::int64_t>) {
          output.kind = GNEISS_PROPERTY_KIND_INT64;
          output.payload.int64_value = payload;
        } else if constexpr (std::is_same_v<Payload, std::uint64_t>) {
          output.kind = GNEISS_PROPERTY_KIND_UINT64;
          output.payload.uint64_value = payload;
        } else if constexpr (std::is_same_v<Payload, float>) {
          output.kind = GNEISS_PROPERTY_KIND_FLOAT32;
          output.payload.float32_value = payload;
        } else if constexpr (std::is_same_v<Payload, double>) {
          output.kind = GNEISS_PROPERTY_KIND_FLOAT64;
          output.payload.float64_value = payload;
        } else if constexpr (std::is_same_v<Payload, std::string>) {
          output.kind = GNEISS_PROPERTY_KIND_STRING;
          output.payload.string_value = {payload.data(),
                                         static_cast<std::uint32_t>(payload.size())};
        } else if constexpr (std::is_same_v<Payload, std::array<std::uint8_t, 16>>) {
          output.kind = GNEISS_PROPERTY_KIND_TYPE_ID;
          std::ranges::copy(payload, output.payload.type_id_value.bytes);
        } else if constexpr (std::is_same_v<Payload, std::array<float, 3>>) {
          output.kind = GNEISS_PROPERTY_KIND_VEC3;
          output.payload.vec3_value = {payload[0], payload[1], payload[2]};
        } else {
          output.kind = GNEISS_PROPERTY_KIND_QUATERNION;
          output.payload.quaternion_value = {payload[0], payload[1], payload[2], payload[3]};
        }
        return gneiss::result::success;
      },
      source.payload);
}

gneiss::result from_native_value(const gneiss_property_value& source,
                                 gneiss::ipc_property_value& output) noexcept {
  try {
    gneiss::ipc_property_value converted;
    switch (source.kind) {
    case GNEISS_PROPERTY_KIND_BOOL:
      converted.payload = source.payload.bool_value != 0U;
      break;
    case GNEISS_PROPERTY_KIND_INT64:
      converted.payload = source.payload.int64_value;
      break;
    case GNEISS_PROPERTY_KIND_UINT64:
      converted.payload = source.payload.uint64_value;
      break;
    case GNEISS_PROPERTY_KIND_FLOAT32:
      converted.payload = source.payload.float32_value;
      break;
    case GNEISS_PROPERTY_KIND_FLOAT64:
      converted.payload = source.payload.float64_value;
      break;
    case GNEISS_PROPERTY_KIND_STRING:
      if (source.payload.string_value.length != 0U && source.payload.string_value.data == nullptr) {
        return gneiss::result::invalid_argument;
      }
      converted.payload =
          source.payload.string_value.length == 0U
              ? std::string{}
              : std::string(source.payload.string_value.data, source.payload.string_value.length);
      break;
    case GNEISS_PROPERTY_KIND_TYPE_ID: {
      std::array<std::uint8_t, 16> bytes{};
      std::ranges::copy(source.payload.type_id_value.bytes, bytes.begin());
      converted.payload = bytes;
      break;
    }
    case GNEISS_PROPERTY_KIND_VEC3:
      converted.payload = std::array{source.payload.vec3_value.x, source.payload.vec3_value.y,
                                     source.payload.vec3_value.z};
      break;
    case GNEISS_PROPERTY_KIND_QUATERNION:
      converted.payload =
          std::array{source.payload.quaternion_value.x, source.payload.quaternion_value.y,
                     source.payload.quaternion_value.z, source.payload.quaternion_value.w};
      break;
    default:
      return gneiss::result::unsupported;
    }
    output = std::move(converted);
    return gneiss::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::result::out_of_memory;
  } catch (...) {
    return gneiss::result::internal;
  }
}

} // namespace

namespace gneiss::runtime_internal {

runtime_property_editor::~runtime_property_editor() { reset(); }

result runtime_property_editor::initialize(gneiss_world world, runtime_scene_inspection& inspection,
                                           std::uint64_t session_id) noexcept {
  if (world == GNEISS_NULL_WORLD || session_id == 0U || registry_ != GNEISS_NULL_TYPE_REGISTRY) {
    return result::invalid_argument;
  }
  gneiss_type_registry registry = GNEISS_NULL_TYPE_REGISTRY;
  auto operation = gneiss_type_registry_create(&registry);
  if (operation == GNEISS_SUCCESS) {
    operation = gneiss_world_register_reflection(registry);
  }
  if (operation == GNEISS_SUCCESS) {
    operation = gneiss_type_registry_freeze(registry);
  }
  if (operation != GNEISS_SUCCESS) {
    if (registry != GNEISS_NULL_TYPE_REGISTRY) {
      (void)gneiss_type_registry_destroy(registry);
    }
    return from_native(operation);
  }
  world_ = world;
  inspection_ = &inspection;
  session_id_ = session_id;
  registry_ = registry;
  return result::success;
}

void runtime_property_editor::reset() noexcept {
  if (registry_ != GNEISS_NULL_TYPE_REGISTRY) {
    (void)gneiss_type_registry_destroy(registry_);
  }
  registry_ = GNEISS_NULL_TYPE_REGISTRY;
  world_ = GNEISS_NULL_WORLD;
  inspection_ = nullptr;
  session_id_ = 0U;
  revisions_.clear();
}

result runtime_property_editor::execute(const ipc_property_write& command,
                                        ipc_property_write_result& output) noexcept {
  if (registry_ == GNEISS_NULL_TYPE_REGISTRY || inspection_ == nullptr ||
      command.command_id == 0U) {
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

  gneiss_field_info field = GNEISS_FIELD_INFO_INIT;
  auto native =
      gneiss_type_registry_find_field(registry_, command.type_id, command.field_id, &field);
  if (native != GNEISS_SUCCESS ||
      (field.property_capabilities & GNEISS_PROPERTY_CAPABILITY_WRITABLE) == 0U) {
    response.code = native == GNEISS_SUCCESS ? GNEISS_ERROR_UNSUPPORTED : native;
    response.message = native == GNEISS_SUCCESS ? "属性不可写" : "属性不存在";
    output = std::move(response);
    return result::success;
  }
  if (revision->second == std::numeric_limits<std::uint64_t>::max()) {
    response.code = GNEISS_ERROR_OUT_OF_MEMORY;
    response.message = "属性修订号已耗尽";
    output = std::move(response);
    return result::success;
  }
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  operation = to_native_value(command.value, value);
  if (operation == result::success) {
    const gneiss_property_target target{world_, entity};
    native = gneiss_type_registry_set_property(registry_, command.type_id, command.field_id, target,
                                               &value);
    if (native == GNEISS_SUCCESS) {
      gneiss_property_value canonical = GNEISS_PROPERTY_VALUE_INIT;
      native = gneiss_type_registry_get_property(registry_, command.type_id, command.field_id,
                                                 target, &canonical);
      if (native == GNEISS_SUCCESS) {
        operation = from_native_value(canonical, response.canonical_value);
      }
    }
  }
  if (operation != result::success || native != GNEISS_SUCCESS) {
    response.code = operation != result::success ? to_native(operation) : native;
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
