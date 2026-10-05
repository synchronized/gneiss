// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "reflection/type_registry.hpp"

#include "core/rid_table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gneiss::reflection_internal {
namespace {

using type_id_bytes = std::array<std::uint8_t, 16>;

[[nodiscard]] type_id_bytes to_bytes(gneiss_type_id id) noexcept {
  type_id_bytes result{};
  std::ranges::copy(id.bytes, result.begin());
  return result;
}

[[nodiscard]] bool ids_equal(gneiss_type_id left, gneiss_type_id right) noexcept {
  return to_bytes(left) == to_bytes(right);
}

[[nodiscard]] bool consume_utf8_sequence(std::string_view text, std::size_t& index) noexcept {
  const auto first = static_cast<unsigned char>(text[index]);
  std::size_t count = 0;
  std::uint32_t value = 0;
  if (first >= 0xC2U && first <= 0xDFU) {
    count = 1;
    value = first & 0x1FU;
  } else if (first >= 0xE0U && first <= 0xEFU) {
    count = 2;
    value = first & 0x0FU;
  } else if (first >= 0xF0U && first <= 0xF4U) {
    count = 3;
    value = first & 0x07U;
  } else {
    return false;
  }
  if (index + count >= text.size()) {
    return false;
  }
  for (std::size_t offset = 1; offset <= count; ++offset) {
    const auto next = static_cast<unsigned char>(text[index + offset]);
    if ((next & 0xC0U) != 0x80U) {
      return false;
    }
    value = (value << 6U) | (next & 0x3FU);
  }
  if ((count == 2U && value < 0x800U) || (count == 3U && value < 0x10000U) || value > 0x10FFFFU ||
      (value >= 0xD800U && value <= 0xDFFFU)) {
    return false;
  }
  index += count + 1U;
  return true;
}

[[nodiscard]] bool is_valid_name(std::string_view text) noexcept {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first == 0U) {
      return false;
    }
    if (first < 0x80U) {
      ++index;
    } else if (!consume_utf8_sequence(text, index)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_valid_property_kind(std::uint32_t kind) noexcept {
  return kind >= GNEISS_PROPERTY_KIND_BOOL && kind <= GNEISS_PROPERTY_KIND_QUATERNION;
}

[[nodiscard]] bool is_valid_property_value(const gneiss_property_value& value) noexcept {
  if (value.struct_size < sizeof(gneiss_property_value) || !is_valid_property_kind(value.kind)) {
    return false;
  }
  switch (value.kind) {
  case GNEISS_PROPERTY_KIND_BOOL:
    return value.payload.bool_value <= UINT8_C(1);
  case GNEISS_PROPERTY_KIND_FLOAT32:
    return std::isfinite(value.payload.float32_value);
  case GNEISS_PROPERTY_KIND_FLOAT64:
    return std::isfinite(value.payload.float64_value);
  case GNEISS_PROPERTY_KIND_STRING: {
    const auto& string = value.payload.string_value;
    return (string.data != nullptr || string.length == 0U) &&
           is_valid_name(
               std::string_view(string.data == nullptr ? "" : string.data, string.length));
  }
  case GNEISS_PROPERTY_KIND_TYPE_ID:
    return valid_type_id(value.payload.type_id_value);
  case GNEISS_PROPERTY_KIND_VEC3:
    return std::isfinite(value.payload.vec3_value.x) && std::isfinite(value.payload.vec3_value.y) &&
           std::isfinite(value.payload.vec3_value.z);
  case GNEISS_PROPERTY_KIND_QUATERNION:
    return std::isfinite(value.payload.quaternion_value.x) &&
           std::isfinite(value.payload.quaternion_value.y) &&
           std::isfinite(value.payload.quaternion_value.z) &&
           std::isfinite(value.payload.quaternion_value.w);
  default:
    return true;
  }
}

struct property_accessor {
  std::uint32_t kind = GNEISS_PROPERTY_KIND_INVALID;
  gneiss_property_getter getter = nullptr;
  gneiss_property_setter setter = nullptr;
  void* user_data = nullptr;
};

struct field_record {
  gneiss_field_id id = GNEISS_NULL_FIELD_ID;
  gneiss_type_id value_type_id{};
  std::uint32_t flags = 0;
  std::string name;
  property_accessor accessor{};
  gneiss_field_info info{};
};

struct type_record {
  gneiss_type_id id{};
  std::uint32_t schema_version = 0;
  std::string name;
  std::vector<field_record> fields;
  std::vector<gneiss_field_info> field_infos;
  gneiss_type_info info{};
};

[[nodiscard]] bool validate_field(const field_definition& field) noexcept {
  return field.id != GNEISS_NULL_FIELD_ID && valid_type_id(field.value_type_id) &&
         (field.flags & ~GNEISS_FIELD_FLAG_READ_ONLY) == 0U && !field.name.empty() &&
         field.name.size() <= std::numeric_limits<std::uint32_t>::max() &&
         is_valid_name(field.name);
}
[[nodiscard]] bool validate_type(const type_definition& desc) noexcept {
  if (!valid_type_id(desc.id) || desc.schema_version == 0U || desc.name.empty() ||
      desc.name.size() > std::numeric_limits<std::uint32_t>::max() ||
      desc.fields.size() > std::numeric_limits<std::uint32_t>::max() || !is_valid_name(desc.name)) {
    return false;
  }
  for (std::size_t index = 0; index < desc.fields.size(); ++index) {
    if (!validate_field(desc.fields[index])) {
      return false;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      const auto& left = desc.fields[previous];
      const auto& right = desc.fields[index];
      if (left.id == right.id || left.name == right.name) {
        return false;
      }
    }
  }
  return true;
}
[[nodiscard]] bool fields_equal(const field_record& stored,
                                const field_definition& input) noexcept {
  return stored.id == input.id && ids_equal(stored.value_type_id, input.value_type_id) &&
         stored.flags == input.flags && stored.name == input.name;
}
[[nodiscard]] bool types_equal(const type_record& stored, const type_definition& input) noexcept {
  if (stored.schema_version != input.schema_version || stored.name != input.name ||
      stored.fields.size() != input.fields.size()) {
    return false;
  }
  return std::ranges::all_of(stored.fields, [&input](const field_record& field) {
    const auto found = std::ranges::find(input.fields, field.id, &field_definition::id);
    return found != input.fields.end() && fields_equal(field, *found);
  });
}
} // namespace

class type_registry_state final {
public:
  [[nodiscard]] gneiss_result register_type(const type_definition& desc) noexcept {
    if (!validate_type(desc)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      auto* const found = find_type_unlocked(desc.id);
      if (found != nullptr) {
        return types_equal(*found, desc) ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_ARGUMENT;
      }

      auto record = std::make_unique<type_record>();
      record->id = desc.id;
      record->schema_version = desc.schema_version;
      record->name.assign(desc.name);
      record->fields.reserve(desc.fields.size());
      for (const auto& field : desc.fields) {
        record->fields.push_back({.id = field.id,
                                  .value_type_id = field.value_type_id,
                                  .flags = field.flags,
                                  .name = std::string(field.name),
                                  .accessor = {},
                                  .info = {}});
      }
      types_.push_back(std::move(record));
      return GNEISS_SUCCESS;
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  [[nodiscard]] gneiss_result bind_property(gneiss_type_id type_id, gneiss_field_id field_id,
                                            const property_binding& desc) noexcept {
    if (!is_valid_property_kind(desc.kind) || (desc.getter == nullptr && desc.setter == nullptr)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      auto* type = find_type_unlocked(type_id);
      if (type == nullptr) {
        return GNEISS_ERROR_NOT_FOUND;
      }
      auto found = std::ranges::find_if(
          type->fields, [field_id](const field_record& field) { return field.id == field_id; });
      if (found == type->fields.end()) {
        return GNEISS_ERROR_NOT_FOUND;
      }
      if ((found->flags & GNEISS_FIELD_FLAG_READ_ONLY) != 0U && desc.setter != nullptr) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      const property_accessor requested{.kind = desc.kind,
                                        .getter = desc.getter,
                                        .setter = desc.setter,
                                        .user_data = desc.user_data};
      if (found->accessor.kind != GNEISS_PROPERTY_KIND_INVALID) {
        const auto& current = found->accessor;
        return current.kind == requested.kind && current.getter == requested.getter &&
                       current.setter == requested.setter &&
                       current.user_data == requested.user_data
                   ? GNEISS_SUCCESS
                   : GNEISS_ERROR_INVALID_ARGUMENT;
      }
      found->accessor = requested;
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  [[nodiscard]] gneiss_result freeze() noexcept {
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return GNEISS_SUCCESS;
      }
      std::ranges::sort(types_, [](const auto& left, const auto& right) {
        return to_bytes(left->id) < to_bytes(right->id);
      });
      for (auto& type : types_) {
        std::ranges::sort(type->fields, [](const field_record& left, const field_record& right) {
          return left.id < right.id;
        });
        type->field_infos.clear();
        type->field_infos.reserve(type->fields.size());
        for (auto& field : type->fields) {
          field.info = {
              .struct_size = sizeof(gneiss_field_info),
              .id = field.id,
              .value_type_id = field.value_type_id,
              .flags = field.flags,
              .name = field.name.data(),
              .name_length = static_cast<std::uint32_t>(field.name.size()),
              .property_kind = field.accessor.kind,
              .property_capabilities =
                  (field.accessor.getter != nullptr ? GNEISS_PROPERTY_CAPABILITY_READABLE : 0U) |
                  (field.accessor.setter != nullptr ? GNEISS_PROPERTY_CAPABILITY_WRITABLE : 0U)};
          type->field_infos.push_back(field.info);
        }
        type->info = {.struct_size = sizeof(gneiss_type_info),
                      .id = type->id,
                      .schema_version = type->schema_version,
                      .name = type->name.data(),
                      .name_length = static_cast<std::uint32_t>(type->name.size()),
                      .fields = type->field_infos.empty() ? nullptr : type->field_infos.data(),
                      .field_count = static_cast<std::uint32_t>(type->fields.size())};
      }
      is_frozen_ = true;
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  [[nodiscard]] gneiss_result get_property(gneiss_type_id type_id, gneiss_field_id field_id,
                                           gneiss_property_target target,
                                           gneiss_property_value& output) const noexcept {
    property_accessor accessor;
    const auto lookup = find_accessor(type_id, field_id, accessor);
    if (lookup != GNEISS_SUCCESS) {
      return lookup;
    }
    if (accessor.getter == nullptr) {
      return GNEISS_ERROR_UNSUPPORTED;
    }
    output = GNEISS_PROPERTY_VALUE_INIT;
    gneiss_result result = GNEISS_ERROR_INTERNAL;
    try {
      result = accessor.getter(accessor.user_data, target, &output);
    } catch (...) {
      output = GNEISS_PROPERTY_VALUE_INIT;
      return GNEISS_ERROR_INTERNAL;
    }
    if (result != GNEISS_SUCCESS) {
      output = GNEISS_PROPERTY_VALUE_INIT;
      return result;
    }
    if (output.kind != accessor.kind || !is_valid_property_value(output)) {
      output = GNEISS_PROPERTY_VALUE_INIT;
      return GNEISS_ERROR_INTERNAL;
    }
    return GNEISS_SUCCESS;
  }

  [[nodiscard]] gneiss_result set_property(gneiss_type_id type_id, gneiss_field_id field_id,
                                           gneiss_property_target target,
                                           const gneiss_property_value& value) const noexcept {
    if (!is_valid_property_value(value)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    property_accessor accessor;
    const auto lookup = find_accessor(type_id, field_id, accessor);
    if (lookup != GNEISS_SUCCESS) {
      return lookup;
    }
    if (accessor.setter == nullptr) {
      return GNEISS_ERROR_UNSUPPORTED;
    }
    if (value.kind != accessor.kind) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    try {
      return accessor.setter(accessor.user_data, target, &value);
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  [[nodiscard]] gneiss_result is_frozen(std::uint8_t& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      output = is_frozen_ ? UINT8_C(1) : UINT8_C(0);
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  [[nodiscard]] gneiss_result type_count(std::uint32_t& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return GNEISS_ERROR_NOT_READY;
      }
      output = static_cast<std::uint32_t>(types_.size());
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  [[nodiscard]] gneiss_result type_at(std::uint32_t index,
                                      gneiss_type_info& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return GNEISS_ERROR_NOT_READY;
      }
      if (index >= types_.size()) {
        return GNEISS_ERROR_NOT_FOUND;
      }
      output = types_[index]->info;
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  [[nodiscard]] gneiss_result find_type(gneiss_type_id id,
                                        gneiss_type_info& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return GNEISS_ERROR_NOT_READY;
      }
      const auto* found = find_type_unlocked(id);
      if (found == nullptr) {
        return GNEISS_ERROR_NOT_FOUND;
      }
      output = found->info;
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  [[nodiscard]] gneiss_result find_field(gneiss_type_id type_id, gneiss_field_id field_id,
                                         gneiss_field_info& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return GNEISS_ERROR_NOT_READY;
      }
      const auto* type = find_type_unlocked(type_id);
      if (type == nullptr) {
        return GNEISS_ERROR_NOT_FOUND;
      }
      const auto found = std::ranges::find_if(
          type->fields, [field_id](const field_record& field) { return field.id == field_id; });
      if (found == type->fields.end()) {
        return GNEISS_ERROR_NOT_FOUND;
      }
      output = found->info;
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

private:
  [[nodiscard]] gneiss_result find_accessor(gneiss_type_id type_id, gneiss_field_id field_id,
                                            property_accessor& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return GNEISS_ERROR_NOT_READY;
      }
      const auto* type = find_type_unlocked(type_id);
      if (type == nullptr) {
        return GNEISS_ERROR_NOT_FOUND;
      }
      const auto found = std::ranges::find_if(
          type->fields, [field_id](const field_record& field) { return field.id == field_id; });
      if (found == type->fields.end()) {
        return GNEISS_ERROR_NOT_FOUND;
      }
      if (found->accessor.kind == GNEISS_PROPERTY_KIND_INVALID) {
        return GNEISS_ERROR_UNSUPPORTED;
      }
      output = found->accessor;
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  [[nodiscard]] type_record* find_type_unlocked(gneiss_type_id id) noexcept {
    const auto found =
        std::ranges::find_if(types_, [id](const auto& type) { return ids_equal(type->id, id); });
    return found == types_.end() ? nullptr : found->get();
  }

  [[nodiscard]] const type_record* find_type_unlocked(gneiss_type_id id) const noexcept {
    const auto found =
        std::ranges::find_if(types_, [id](const auto& type) { return ids_equal(type->id, id); });
    return found == types_.end() ? nullptr : found->get();
  }

  mutable std::shared_mutex mutex_;
  std::vector<std::unique_ptr<type_record>> types_;
  bool is_frozen_ = false;
};

namespace {

using registry_pointer = std::shared_ptr<type_registry_state>;

std::mutex registry_table_mutex;
gneiss::core::rid_table<registry_pointer> registry_table(UINT16_C(0x5459));

[[nodiscard]] registry_pointer get_registry(gneiss_type_registry registry) {
  std::scoped_lock lock(registry_table_mutex);
  const auto* stored = registry_table.get(registry, gneiss::core::resource_type::type_registry);
  return stored == nullptr ? registry_pointer{} : *stored;
}

} // namespace

registry_ref resolve(gneiss_type_registry registry) { return get_registry(registry); }
gneiss_result create(gneiss_type_registry& output) noexcept {
  output = GNEISS_NULL_TYPE_REGISTRY;
  try {
    auto registry = std::make_shared<type_registry_state>();
    std::scoped_lock lock(registry_table_mutex);
    return registry_table.create(gneiss::core::resource_type::type_registry, std::move(registry),
                                 &output);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result destroy(gneiss_type_registry registry) noexcept {
  try {
    std::scoped_lock lock(registry_table_mutex);
    return registry_table.destroy(registry, gneiss::core::resource_type::type_registry);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result register_type(const registry_ref& state, const type_definition& desc) noexcept {
  return state ? state->register_type(desc) : GNEISS_ERROR_INVALID_HANDLE;
}
gneiss_result bind_property(const registry_ref& state, gneiss_type_id type_id,
                            gneiss_field_id field_id, const property_binding& desc) noexcept {
  return state ? state->bind_property(type_id, field_id, desc) : GNEISS_ERROR_INVALID_HANDLE;
}
gneiss_result freeze(const registry_ref& state) noexcept {
  return state ? state->freeze() : GNEISS_ERROR_INVALID_HANDLE;
}
gneiss_result is_frozen(const registry_ref& state, std::uint8_t& output) noexcept {
  return state ? state->is_frozen(output) : GNEISS_ERROR_INVALID_HANDLE;
}
gneiss_result type_count(const registry_ref& state, std::uint32_t& output) noexcept {
  return state ? state->type_count(output) : GNEISS_ERROR_INVALID_HANDLE;
}
gneiss_result type_at(const registry_ref& state, std::uint32_t index,
                      gneiss_type_info& output) noexcept {
  return state ? state->type_at(index, output) : GNEISS_ERROR_INVALID_HANDLE;
}
gneiss_result find_type(const registry_ref& state, gneiss_type_id id,
                        gneiss_type_info& output) noexcept {
  return state ? state->find_type(id, output) : GNEISS_ERROR_INVALID_HANDLE;
}
gneiss_result find_field(const registry_ref& state, gneiss_type_id type_id,
                         gneiss_field_id field_id, gneiss_field_info& output) noexcept {
  return state ? state->find_field(type_id, field_id, output) : GNEISS_ERROR_INVALID_HANDLE;
}
gneiss_result get_property(const registry_ref& state, gneiss_type_id type_id,
                           gneiss_field_id field_id, gneiss_property_target target,
                           gneiss_property_value& output) noexcept {
  return state ? state->get_property(type_id, field_id, target, output)
               : GNEISS_ERROR_INVALID_HANDLE;
}
gneiss_result set_property(const registry_ref& state, gneiss_type_id type_id,
                           gneiss_field_id field_id, gneiss_property_target target,
                           const gneiss_property_value& value) noexcept {
  return state ? state->set_property(type_id, field_id, target, value)
               : GNEISS_ERROR_INVALID_HANDLE;
}
} // namespace gneiss::reflection_internal
