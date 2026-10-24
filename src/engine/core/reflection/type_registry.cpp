// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/reflection/type_registry.hpp"
#include "engine/core/result.hpp"
#include <gneiss/engine/detail/reflection_values.hpp>

#include "engine/core/rid_table.hpp"

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

namespace gneiss::engine {
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

template <class R, class... Args, class Identity>
bool same_callback(const std::function<R(Args...)>& left, const std::function<R(Args...)>& right,
                   Identity left_id, Identity right_id) {
  if (!left || !right)
    return !left && !right;
  if (left_id || right_id)
    return left_id == right_id;
  using pointer = R (*)(Args...);
  const auto* a = left.template target<pointer>();
  const auto* b = right.template target<pointer>();
  return a && b && *a == *b;
}

struct property_accessor {
  std::uint32_t kind = GNEISS_PROPERTY_KIND_INVALID;
  decltype(property_binding::getter) getter;
  decltype(property_binding::setter) setter;
  gneiss_property_getter getter_identity{};
  gneiss_property_setter setter_identity{};
  void* user_data = nullptr;
  candidate_property_setter candidate_setter = nullptr;
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
  [[nodiscard]] gneiss::engine::result register_type(const type_definition& desc) noexcept {
    if (!validate_type(desc)) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return gneiss::engine::result::invalid_state;
      }
      auto* const found = find_type_unlocked(desc.id);
      if (found != nullptr) {
        return types_equal(*found, desc) ? gneiss::engine::result::success
                                         : gneiss::engine::result::invalid_argument;
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
      return gneiss::engine::result::success;
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result bind_property(gneiss_type_id type_id,
                                                     gneiss_field_id field_id,
                                                     const property_binding& desc) noexcept {
    if (!is_valid_property_kind(desc.kind) ||
        (desc.getter == nullptr && desc.setter == nullptr && desc.candidate_setter == nullptr)) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return gneiss::engine::result::invalid_state;
      }
      auto* type = find_type_unlocked(type_id);
      if (type == nullptr) {
        return gneiss::engine::result::not_found;
      }
      auto found = std::ranges::find_if(
          type->fields, [field_id](const field_record& field) { return field.id == field_id; });
      if (found == type->fields.end()) {
        return gneiss::engine::result::not_found;
      }
      if ((found->flags & GNEISS_FIELD_FLAG_READ_ONLY) != 0U &&
          (desc.setter != nullptr || desc.candidate_setter != nullptr)) {
        return gneiss::engine::result::invalid_argument;
      }
      const property_accessor requested{
          .kind = desc.kind,
          .getter = desc.getter,
          .setter = desc.setter,
          .getter_identity = desc.getter_identity,
          .setter_identity = desc.setter_identity,
          .user_data = desc.user_data,
          .candidate_setter = desc.candidate_setter,
      };
      if (found->accessor.kind != GNEISS_PROPERTY_KIND_INVALID) {
        const auto& current = found->accessor;
        return current.kind == requested.kind &&
                       same_callback(current.getter, requested.getter, current.getter_identity,
                                     requested.getter_identity) &&
                       same_callback(current.setter, requested.setter, current.setter_identity,
                                     requested.setter_identity) &&
                       current.user_data == requested.user_data &&
                       current.candidate_setter == requested.candidate_setter
                   ? gneiss::engine::result::success
                   : gneiss::engine::result::invalid_argument;
      }
      found->accessor = requested;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result freeze() noexcept {
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return gneiss::engine::result::success;
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
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result get_property(gneiss_type_id type_id,
                                                    gneiss_field_id field_id,
                                                    gneiss_property_target target,
                                                    gneiss_property_value& output) const noexcept {
    property_accessor accessor;
    const auto lookup = find_accessor(type_id, field_id, accessor);
    if (lookup != gneiss::engine::result::success) {
      return lookup;
    }
    if (accessor.getter == nullptr) {
      return gneiss::engine::result::unsupported;
    }
    output = GNEISS_PROPERTY_VALUE_INIT;
    gneiss::engine::result result = gneiss::engine::result::internal;
    try {
      result = accessor.getter(accessor.user_data, target, &output);
    } catch (...) {
      output = GNEISS_PROPERTY_VALUE_INIT;
      return gneiss::engine::result::internal;
    }
    if (result != gneiss::engine::result::success) {
      output = GNEISS_PROPERTY_VALUE_INIT;
      return result;
    }
    if (output.kind != accessor.kind || !is_valid_property_value(output)) {
      output = GNEISS_PROPERTY_VALUE_INIT;
      return gneiss::engine::result::internal;
    }
    return gneiss::engine::result::success;
  }

  [[nodiscard]] gneiss::engine::result
  set_property(gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
               const gneiss_property_value& value) const noexcept {
    if (!is_valid_property_value(value)) {
      return gneiss::engine::result::invalid_argument;
    }
    property_accessor accessor;
    const auto lookup = find_accessor(type_id, field_id, accessor);
    if (lookup != gneiss::engine::result::success) {
      return lookup;
    }
    if (accessor.setter == nullptr) {
      return gneiss::engine::result::unsupported;
    }
    if (value.kind != accessor.kind) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      return accessor.setter(accessor.user_data, target, &value);
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result
  set_candidate_property(gneiss_type_id type_id, gneiss_field_id field_id, void* candidate,
                         const gneiss_property_value& value) const noexcept {
    if (candidate == nullptr || !is_valid_property_value(value)) {
      return gneiss::engine::result::invalid_argument;
    }
    property_accessor accessor;
    const auto lookup = find_accessor(type_id, field_id, accessor);
    if (lookup != gneiss::engine::result::success) {
      return lookup;
    }
    if (accessor.candidate_setter == nullptr) {
      return gneiss::engine::result::unsupported;
    }
    if (value.kind != accessor.kind) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      return accessor.candidate_setter(accessor.user_data, candidate, value);
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result is_frozen(std::uint8_t& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      output = is_frozen_ ? UINT8_C(1) : UINT8_C(0);
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result type_count(std::uint32_t& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return gneiss::engine::result::not_ready;
      }
      output = static_cast<std::uint32_t>(types_.size());
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result type_at(std::uint32_t index,
                                               gneiss_type_info& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return gneiss::engine::result::not_ready;
      }
      if (index >= types_.size()) {
        return gneiss::engine::result::not_found;
      }
      output = types_[index]->info;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result find_type(gneiss_type_id id,
                                                 gneiss_type_info& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return gneiss::engine::result::not_ready;
      }
      const auto* found = find_type_unlocked(id);
      if (found == nullptr) {
        return gneiss::engine::result::not_found;
      }
      output = found->info;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result find_field(gneiss_type_id type_id, gneiss_field_id field_id,
                                                  gneiss_field_info& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return gneiss::engine::result::not_ready;
      }
      const auto* type = find_type_unlocked(type_id);
      if (type == nullptr) {
        return gneiss::engine::result::not_found;
      }
      const auto found = std::ranges::find_if(
          type->fields, [field_id](const field_record& field) { return field.id == field_id; });
      if (found == type->fields.end()) {
        return gneiss::engine::result::not_found;
      }
      output = found->info;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

private:
  [[nodiscard]] gneiss::engine::result find_accessor(gneiss_type_id type_id,
                                                     gneiss_field_id field_id,
                                                     property_accessor& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return gneiss::engine::result::not_ready;
      }
      const auto* type = find_type_unlocked(type_id);
      if (type == nullptr) {
        return gneiss::engine::result::not_found;
      }
      const auto found = std::ranges::find_if(
          type->fields, [field_id](const field_record& field) { return field.id == field_id; });
      if (found == type->fields.end()) {
        return gneiss::engine::result::not_found;
      }
      if (found->accessor.kind == GNEISS_PROPERTY_KIND_INVALID) {
        return gneiss::engine::result::unsupported;
      }
      output = found->accessor;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
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
gneiss::engine::rid_table<registry_pointer> registry_table(UINT16_C(0x5459));

[[nodiscard]] registry_pointer get_registry(gneiss_type_registry registry) {
  std::scoped_lock lock(registry_table_mutex);
  const auto* stored = registry_table.get(registry, gneiss::engine::resource_type::type_registry);
  return stored == nullptr ? registry_pointer{} : *stored;
}

} // namespace

registry_ref resolve(gneiss_type_registry registry) { return get_registry(registry); }
gneiss::engine::result create_native(registry_ref& output) noexcept {
  try {
    output = std::make_shared<type_registry_state>();
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result create_type_registry(gneiss_type_registry& output) noexcept {
  output = GNEISS_NULL_TYPE_REGISTRY;
  try {
    registry_ref registry;
    const auto status = create_native(registry);
    if (status != gneiss::engine::result::success) {
      return status;
    }
    std::scoped_lock lock(registry_table_mutex);
    return registry_table.create(gneiss::engine::resource_type::type_registry, std::move(registry),
                                 &output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result destroy_type_registry(gneiss_type_registry registry) noexcept {
  try {
    std::scoped_lock lock(registry_table_mutex);
    return registry_table.destroy(registry, gneiss::engine::resource_type::type_registry);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result register_type(const registry_ref& state,
                                     const type_definition& desc) noexcept {
  return state ? state->register_type(desc) : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result bind_property(const registry_ref& state, gneiss_type_id type_id,
                                     gneiss_field_id field_id,
                                     const property_binding& desc) noexcept {
  return state ? state->bind_property(type_id, field_id, desc)
               : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result freeze(const registry_ref& state) noexcept {
  return state ? state->freeze() : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result is_frozen(const registry_ref& state, std::uint8_t& output) noexcept {
  return state ? state->is_frozen(output) : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result type_count(const registry_ref& state, std::uint32_t& output) noexcept {
  return state ? state->type_count(output) : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result type_at(const registry_ref& state, std::uint32_t index,
                               gneiss_type_info& output) noexcept {
  return state ? state->type_at(index, output) : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result find_type(const registry_ref& state, gneiss_type_id id,
                                 gneiss_type_info& output) noexcept {
  return state ? state->find_type(id, output) : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result find_field(const registry_ref& state, gneiss_type_id type_id,
                                  gneiss_field_id field_id, gneiss_field_info& output) noexcept {
  return state ? state->find_field(type_id, field_id, output)
               : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result get_property(const registry_ref& state, gneiss_type_id type_id,
                                    gneiss_field_id field_id, gneiss_property_target target,
                                    gneiss_property_value& output) noexcept {
  return state ? state->get_property(type_id, field_id, target, output)
               : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result set_property(const registry_ref& state, gneiss_type_id type_id,
                                    gneiss_field_id field_id, gneiss_property_target target,
                                    const gneiss_property_value& value) noexcept {
  return state ? state->set_property(type_id, field_id, target, value)
               : gneiss::engine::result::invalid_handle;
}

gneiss::engine::result set_candidate_property(const registry_ref& state, gneiss_type_id type_id,
                                              gneiss_field_id field_id, void* candidate,
                                              const gneiss_property_value& value) noexcept {
  return state ? state->set_candidate_property(type_id, field_id, candidate, value)
               : gneiss::engine::result::invalid_handle;
}
} // namespace gneiss::engine

namespace gneiss::engine {
gneiss::engine::result describe_type(const registry_ref& registry, type_id id,
                                     type_info& output) noexcept try {
  gneiss_type_info source{};
  const auto operation = find_type(registry, to_native(id), source);
  if (operation.failed()) {
    return operation;
  }
  type_info pending{
      .id = from_native(source.id),
      .schema_version = source.schema_version,
      .name = {source.name, source.name_length},
      .fields = {},
  };
  pending.fields.reserve(source.field_count);
  for (std::uint32_t index = 0; index < source.field_count; ++index) {
    const auto& field = source.fields[index];
    pending.fields.push_back({
        .id = field_id{field.id},
        .value_type_id = from_native(field.value_type_id),
        .flags = static_cast<field_flags>(field.flags),
        .name = {field.name, field.name_length},
        .kind = static_cast<property_kind>(field.property_kind),
        .capabilities = static_cast<property_capabilities>(field.property_capabilities),
    });
  }
  output = std::move(pending);
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
gneiss::engine::result describe_field(const registry_ref& registry, type_id type, field_id field,
                                      field_info& output) noexcept {
  gneiss_field_info source{};
  const auto operation = find_field(registry, to_native(type), field.get(), source);
  if (operation.failed()) {
    return operation;
  }
  output = {
      .id = field_id{source.id},
      .value_type_id = from_native(source.value_type_id),
      .flags = static_cast<field_flags>(source.flags),
      .name = {source.name, source.name_length},
      .kind = static_cast<property_kind>(source.property_kind),
      .capabilities = static_cast<property_capabilities>(source.property_capabilities),
  };
  return gneiss::engine::result::success;
}
gneiss::engine::result read_property(const registry_ref& registry, type_id type, field_id field,
                                     property_target target, property_value& output) noexcept try {
  gneiss_property_value raw = GNEISS_PROPERTY_VALUE_INIT;
  auto operation = get_property(registry, to_native(type), field.get(), to_native(target), raw);
  if (operation.failed()) {
    return operation;
  }
  gneiss::property_value borrowed;
  operation = gneiss::engine::result{from_native(raw, borrowed).native()};
  if (operation.failed()) {
    return operation;
  }
  property_value pending;
  std::visit(
      [&](const auto& value) {
        if constexpr (std::same_as<std::decay_t<decltype(value)>, std::string_view>) {
          pending.payload = std::string{value};
        } else {
          pending.payload = value;
        }
      },
      borrowed.payload);
  output = std::move(pending);
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
gneiss::engine::result write_property(const registry_ref& registry, type_id type, field_id field,
                                      property_target target, const property_value& value) noexcept
    try {
  if (value.payload.valueless_by_exception()) {
    return gneiss::engine::result::invalid_argument;
  }
  gneiss::property_value borrowed;
  std::visit(
      [&](const auto& item) {
        if constexpr (std::same_as<std::decay_t<decltype(item)>, std::string>) {
          borrowed.payload = std::string_view{item};
        } else {
          borrowed.payload = item;
        }
      },
      value.payload);
  gneiss_property_value raw = GNEISS_PROPERTY_VALUE_INIT;
  const gneiss::engine::result operation{to_native(borrowed, raw).native()};
  if (operation.failed()) {
    return operation;
  }
  return set_property(registry, to_native(type), field.get(), to_native(target), raw);
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
} // namespace gneiss::engine
