// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/reflection/type_registry.hpp"
#include "engine/core/result.hpp"

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

[[nodiscard]] bool is_valid_property_kind(property_kind kind) noexcept {
  return kind >= gneiss::property_kind::boolean && kind <= gneiss::property_kind::quaternion;
}

[[nodiscard]] bool is_valid_property_value(const gneiss::property_value& value) noexcept {
  if (value.payload.valueless_by_exception())
    return false;
  return std::visit(
      [](const auto& item) noexcept {
        using type = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<type, std::monostate>)
          return false;
        else if constexpr (std::is_floating_point_v<type>)
          return std::isfinite(item);
        else if constexpr (std::is_same_v<type, std::string_view>)
          return is_valid_name(item);
        else if constexpr (std::is_same_v<type, type_guid>)
          return static_cast<bool>(item);
        else if constexpr (std::is_same_v<type, property_vec3>)
          return std::isfinite(item.x) && std::isfinite(item.y) && std::isfinite(item.z);
        else if constexpr (std::is_same_v<type, property_quaternion>)
          return std::isfinite(item.x) && std::isfinite(item.y) && std::isfinite(item.z) &&
                 std::isfinite(item.w);
        else
          return true;
      },
      value.payload);
}

struct property_accessor {
  property_kind kind = gneiss::property_kind::invalid;
  decltype(property_binding::getter) getter;
  decltype(property_binding::setter) setter;
  void* user_data = nullptr;
  candidate_property_setter candidate_setter = nullptr;
};

struct field_record {
  std::uint32_t id = 0U;
  type_guid value_type_id{};
  std::uint32_t flags = 0;
  std::string name;
  property_accessor accessor{};
};

struct type_record {
  type_guid id{};
  std::uint32_t schema_version = 0;
  std::string name;
  std::vector<field_record> fields;
};

[[nodiscard]] bool validate_field(const field_definition& field) noexcept {
  return field.id != 0U && valid_type_id(field.value_type_id) &&
         (field.flags & ~static_cast<std::uint32_t>(field_flags::read_only)) == 0U &&
         !field.name.empty() && field.name.size() <= std::numeric_limits<std::uint32_t>::max() &&
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
  return stored.id == input.id && stored.value_type_id == input.value_type_id &&
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
[[nodiscard]] field_info describe_record(const field_record& field) noexcept {
  return {
      .id = field_id{field.id},
      .value_type_id = field.value_type_id,
      .flags = static_cast<field_flags>(field.flags),
      .name = field.name,
      .kind = field.accessor.kind,
      .capabilities =
          (field.accessor.getter ? property_capabilities::readable : property_capabilities::none) |
          (field.accessor.setter ? property_capabilities::writable : property_capabilities::none)};
}

[[nodiscard]] type_info describe_record(const type_record& record) {
  type_info pending{
      .id = record.id, .schema_version = record.schema_version, .name = record.name, .fields = {}};
  pending.fields.reserve(record.fields.size());
  for (const auto& field : record.fields)
    pending.fields.push_back(describe_record(field));
  return pending;
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
                                  .accessor = {}});
      }
      types_.push_back(std::move(record));
      return gneiss::engine::result::success;
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result bind_property(type_guid type_id, std::uint32_t field_id,
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
      if ((found->flags & static_cast<std::uint32_t>(field_flags::read_only)) != 0U &&
          (desc.setter != nullptr || desc.candidate_setter != nullptr)) {
        return gneiss::engine::result::invalid_argument;
      }
      const property_accessor requested{
          .kind = desc.kind,
          .getter = desc.getter,
          .setter = desc.setter,

          .user_data = desc.user_data,
          .candidate_setter = desc.candidate_setter,
      };
      if (found->accessor.kind != gneiss::property_kind::invalid) {
        const auto& current = found->accessor;
        return current.kind == requested.kind && current.getter == requested.getter &&
                       current.setter == requested.setter &&
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
        return left->id.value.bytes() < right->id.value.bytes();
      });
      for (auto& type : types_) {
        std::ranges::sort(type->fields, [](const field_record& left, const field_record& right) {
          return left.id < right.id;
        });
      }
      is_frozen_ = true;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result get_property(type_guid type_id, std::uint32_t field_id,
                                                    property_target target,
                                                    gneiss::property_value& output) const noexcept {
    property_accessor accessor;
    const auto lookup = find_accessor(type_id, field_id, accessor);
    if (lookup != gneiss::engine::result::success) {
      return lookup;
    }
    if (accessor.getter == nullptr) {
      return gneiss::engine::result::unsupported;
    }
    output = gneiss::property_value{};
    gneiss::engine::result result = gneiss::engine::result::internal;
    try {
      result = accessor.getter(accessor.user_data, target, &output);
    } catch (...) {
      output = gneiss::property_value{};
      return gneiss::engine::result::internal;
    }
    if (result != gneiss::engine::result::success) {
      output = gneiss::property_value{};
      return result;
    }
    if (output.kind() != accessor.kind || !is_valid_property_value(output)) {
      output = gneiss::property_value{};
      return gneiss::engine::result::internal;
    }
    return gneiss::engine::result::success;
  }

  [[nodiscard]] gneiss::engine::result
  set_property(type_guid type_id, std::uint32_t field_id, property_target target,
               const gneiss::property_value& value) const noexcept {
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
    if (value.kind() != accessor.kind) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      return accessor.setter(accessor.user_data, target, &value);
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result
  set_candidate_property(type_guid type_id, std::uint32_t field_id, void* candidate,
                         const gneiss::property_value& value) const noexcept {
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
    if (value.kind() != accessor.kind) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      return accessor.candidate_setter(accessor.user_data, candidate, value);
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result is_frozen(bool& output) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      output = is_frozen_;
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

  [[nodiscard]] result describe_type(type_guid id, type_info& output) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return result::not_ready;
    const auto* record = find_type_unlocked(id);
    if (!record)
      return result::not_found;
    output = describe_record(*record);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }

  [[nodiscard]] result describe_type_at(std::uint32_t index, type_info& output) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return result::not_ready;
    if (index >= types_.size())
      return result::not_found;
    output = describe_record(*types_[index]);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }

  [[nodiscard]] result describe_field(type_guid id, field_id field,
                                      field_info& output) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return result::not_ready;
    const auto* record = find_type_unlocked(id);
    if (!record)
      return result::not_found;
    const auto found = std::ranges::find(record->fields, field.get(), &field_record::id);
    if (found == record->fields.end())
      return result::not_found;
    output = describe_record(*found);
    return result::success;
  } catch (...) {
    return result::internal;
  }

private:
  [[nodiscard]] gneiss::engine::result find_accessor(type_guid type_id, std::uint32_t field_id,
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
      if (found->accessor.kind == gneiss::property_kind::invalid) {
        return gneiss::engine::result::unsupported;
      }
      output = found->accessor;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] type_record* find_type_unlocked(type_guid id) noexcept {
    const auto found =
        std::ranges::find_if(types_, [id](const auto& type) { return type->id == id; });
    return found == types_.end() ? nullptr : found->get();
  }

  [[nodiscard]] const type_record* find_type_unlocked(type_guid id) const noexcept {
    const auto found =
        std::ranges::find_if(types_, [id](const auto& type) { return type->id == id; });
    return found == types_.end() ? nullptr : found->get();
  }

  mutable std::shared_mutex mutex_;
  std::vector<std::unique_ptr<type_record>> types_;
  bool is_frozen_ = false;
};

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
gneiss::engine::result register_type(const registry_ref& state,
                                     const type_definition& desc) noexcept {
  return state ? state->register_type(desc) : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result bind_property(const registry_ref& state, type_guid type_id,
                                     std::uint32_t field_id,
                                     const property_binding& desc) noexcept {
  return state ? state->bind_property(type_id, field_id, desc)
               : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result freeze(const registry_ref& state) noexcept {
  return state ? state->freeze() : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result is_frozen(const registry_ref& state, bool& output) noexcept {
  return state ? state->is_frozen(output) : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result type_count(const registry_ref& state, std::uint32_t& output) noexcept {
  return state ? state->type_count(output) : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result get_property(const registry_ref& state, type_guid type_id,
                                    std::uint32_t field_id, property_target target,
                                    gneiss::property_value& output) noexcept {
  return state ? state->get_property(type_id, field_id, target, output)
               : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result set_property(const registry_ref& state, type_guid type_id,
                                    std::uint32_t field_id, property_target target,
                                    const gneiss::property_value& value) noexcept {
  return state ? state->set_property(type_id, field_id, target, value)
               : gneiss::engine::result::invalid_handle;
}

gneiss::engine::result set_candidate_property(const registry_ref& state, type_guid type_id,
                                              std::uint32_t field_id, void* candidate,
                                              const gneiss::property_value& value) noexcept {
  return state ? state->set_candidate_property(type_id, field_id, candidate, value)
               : gneiss::engine::result::invalid_handle;
}
} // namespace gneiss::engine

namespace gneiss::engine {
result describe_type(const registry_ref& registry, type_guid id, type_info& output) noexcept {
  return registry ? registry->describe_type(id, output) : result::invalid_handle;
}
result describe_type_at(const registry_ref& registry, std::uint32_t index,
                        type_info& output) noexcept {
  return registry ? registry->describe_type_at(index, output) : result::invalid_handle;
}
result describe_field(const registry_ref& registry, type_guid type, field_id field,
                      field_info& output) noexcept {
  return registry ? registry->describe_field(type, field, output) : result::invalid_handle;
}

gneiss::engine::result read_property(const registry_ref& registry, gneiss::engine::type_guid type,
                                     field_id field, property_target target,
                                     property_value& output) noexcept try {
  gneiss::property_value borrowed;
  const auto operation = get_property(registry, type, field.get(), target, borrowed);
  if (operation.failed())
    return operation;
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
gneiss::engine::result write_property(const registry_ref& registry, gneiss::engine::type_guid type,
                                      field_id field, property_target target,
                                      const property_value& value) noexcept try {
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
  return set_property(registry, type, field.get(), target, borrowed);
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
} // namespace gneiss::engine
