// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/meta/type_registration.hpp"
#include "engine/core/result.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gneiss::engine::meta {
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

[[nodiscard]] bool is_valid_property_kind(gneiss::engine::value_kind kind) noexcept {
  return kind >= gneiss::engine::value_kind::boolean &&
         kind <= gneiss::engine::value_kind::quaternion;
}

[[nodiscard]] bool is_valid_property_value(const gneiss::engine::value& value) noexcept {
  if (value.payload.valueless_by_exception())
    return false;
  return std::visit(
      [](const auto& item) noexcept {
        using type = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<type, std::monostate>)
          return false;
        else if constexpr (std::is_floating_point_v<type>)
          return std::isfinite(item);
        else if constexpr (std::is_same_v<type, std::string>)
          return is_valid_name(item);
        else if constexpr (std::is_same_v<type, guid>)
          return static_cast<bool>(item);
        else if constexpr (std::is_same_v<type, gneiss::engine::vector3>)
          return std::isfinite(item.x) && std::isfinite(item.y) && std::isfinite(item.z);
        else if constexpr (std::is_same_v<type, gneiss::engine::quaternion>)
          return std::isfinite(item.x) && std::isfinite(item.y) && std::isfinite(item.z) &&
                 std::isfinite(item.w);
        else
          return true;
      },
      value.payload);
}

struct property_accessor {
  gneiss::engine::value_kind kind = gneiss::engine::value_kind::invalid;
  decltype(property_binding::getter) getter;
  decltype(property_binding::setter) setter;
  void* user_data = nullptr;
};

struct property_record {
  std::uint32_t id = 0U;
  engine::guid type_guid{};
  std::uint32_t flags = 0;
  std::string name;
  property_accessor accessor{};
};

struct type_record {
  engine::guid guid{};
  engine::type_id type_id{};
  std::uint32_t schema_version = 0;
  std::string name;
  std::vector<property_record> properties;
  serialization_binding serialization{};
};

[[nodiscard]] bool validate_property(const property_definition& property) noexcept {
  return property.id.is_valid() && static_cast<bool>(property.type_guid) &&
         (static_cast<std::uint32_t>(property.flags) &
          ~static_cast<std::uint32_t>(property_flags::read_only)) == 0U &&
         !property.name.empty() &&
         property.name.size() <= std::numeric_limits<std::uint32_t>::max() &&
         is_valid_name(property.name);
}
[[nodiscard]] bool validate_type(const type_definition& desc) noexcept {
  if (!static_cast<bool>(desc.guid) || desc.schema_version == 0U || desc.name.empty() ||
      desc.name.size() > std::numeric_limits<std::uint32_t>::max() ||
      desc.properties.size() > std::numeric_limits<std::uint32_t>::max() ||
      !is_valid_name(desc.name)) {
    return false;
  }
  for (std::size_t index = 0; index < desc.properties.size(); ++index) {
    if (!validate_property(desc.properties[index])) {
      return false;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      const auto& left = desc.properties[previous];
      const auto& right = desc.properties[index];
      if (left.id == right.id || left.name == right.name) {
        return false;
      }
    }
  }
  return true;
}
[[nodiscard]] property_info describe_record(const property_record& property) noexcept {
  return {.id = property_id{property.id},
          .type_guid = property.type_guid,
          .flags = static_cast<property_flags>(property.flags),
          .name = property.name,
          .kind = property.accessor.kind,
          .capabilities = (property.accessor.getter ? property_capabilities::readable
                                                    : property_capabilities::none) |
                          (property.accessor.setter ? property_capabilities::writable
                                                    : property_capabilities::none)};
}

[[nodiscard]] type_info describe_record(const type_record& record) {
  type_info pending{.guid = record.guid,
                    .schema_version = record.schema_version,
                    .name = record.name,
                    .properties = {}};
  pending.properties.reserve(record.properties.size());
  for (const auto& property : record.properties)
    pending.properties.push_back(describe_record(property));
  return pending;
}

} // namespace

class type_registry_state final {
public:
  [[nodiscard]] gneiss::engine::result
  register_type(const type_definition& desc,
                std::span<const property_binding> bindings = {}) noexcept {
    if (!validate_type(desc)) {
      return gneiss::engine::result::invalid_argument;
    }
    if (!bindings.empty()) {
      if (bindings.size() != desc.properties.size())
        return result::invalid_argument;
      for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        if (!is_valid_property_kind(binding.kind) || (!binding.getter && !binding.setter) ||
            (desc.properties[i].flags == property_flags::read_only && binding.setter))
          return result::invalid_argument;
      }
    }
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return gneiss::engine::result::invalid_state;
      }
      if (find_type_unlocked(desc.guid) || (desc.type_id && by_cpp_type_.contains(desc.type_id))) {
        return result::invalid_argument;
      }

      auto record = std::make_unique<type_record>();
      record->guid = desc.guid;
      record->type_id = desc.type_id;
      record->schema_version = desc.schema_version;
      record->name.assign(desc.name);
      record->serialization = desc.serialization;
      record->properties.reserve(desc.properties.size());
      for (const auto& property : desc.properties) {
        record->properties.push_back({.id = property.id.get(),
                                      .type_guid = property.type_guid,
                                      .flags = static_cast<std::uint32_t>(property.flags),
                                      .name = std::string(property.name),
                                      .accessor = {}});
      }
      for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        record->properties[i].accessor = {binding.kind, binding.getter, binding.setter,
                                          binding.user_data};
      }
      // 先预留所有者数组容量，索引插入成功后移动所有权不再分配；失败不会留下半条记录。
      types_.reserve(types_.size() + 1);
      if (desc.type_id)
        by_cpp_type_.emplace(desc.type_id, record.get());
      types_.push_back(std::move(record));
      return gneiss::engine::result::success;
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result bind_property(guid type_id, std::uint32_t property_id,
                                                     const property_binding& desc) noexcept {
    if (!is_valid_property_kind(desc.kind) || (desc.getter == nullptr && desc.setter == nullptr)) {
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
      auto found =
          std::ranges::find_if(type->properties, [property_id](const property_record& property) {
            return property.id == property_id;
          });
      if (found == type->properties.end()) {
        return gneiss::engine::result::not_found;
      }
      if ((found->flags & static_cast<std::uint32_t>(property_flags::read_only)) != 0U &&
          (desc.setter != nullptr)) {
        return gneiss::engine::result::invalid_argument;
      }
      const property_accessor requested{
          .kind = desc.kind,
          .getter = desc.getter,
          .setter = desc.setter,

          .user_data = desc.user_data,
      };
      if (found->accessor.kind != gneiss::engine::value_kind::invalid) {
        const auto& current = found->accessor;
        return current.kind == requested.kind && current.getter == requested.getter &&
                       current.setter == requested.setter &&
                       current.user_data == requested.user_data
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
        return left->guid.bytes() < right->guid.bytes();
      });
      for (auto& type : types_) {
        std::ranges::sort(type->properties,
                          [](const property_record& left, const property_record& right) {
                            return left.id < right.id;
                          });
      }
      is_frozen_ = true;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] gneiss::engine::result get_property(guid type_id, std::uint32_t property_id,
                                                    const void* object,
                                                    gneiss::engine::value& output) const noexcept {
    if (!object)
      return result::invalid_argument;
    property_accessor accessor;
    const auto lookup = find_accessor(type_id, property_id, accessor);
    if (lookup != gneiss::engine::result::success) {
      return lookup;
    }
    if (accessor.getter == nullptr) {
      return gneiss::engine::result::unsupported;
    }
    output = gneiss::engine::value{};
    gneiss::engine::result result = gneiss::engine::result::internal;
    try {
      result = accessor.getter(accessor.user_data, object, output);
    } catch (const std::bad_alloc&) {
      output = gneiss::engine::value{};
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      output = gneiss::engine::value{};
      return gneiss::engine::result::internal;
    }
    if (result != gneiss::engine::result::success) {
      output = gneiss::engine::value{};
      return result;
    }
    if (output.kind() != accessor.kind || !is_valid_property_value(output)) {
      output = gneiss::engine::value{};
      return gneiss::engine::result::internal;
    }
    return gneiss::engine::result::success;
  }

  [[nodiscard]] gneiss::engine::result
  set_property(guid type_id, std::uint32_t property_id, void* object,
               const gneiss::engine::value& value) const noexcept {
    if (!is_valid_property_value(value)) {
      return gneiss::engine::result::invalid_argument;
    }
    if (!object)
      return result::invalid_argument;
    property_accessor accessor;
    const auto lookup = find_accessor(type_id, property_id, accessor);
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
      return accessor.setter(accessor.user_data, object, value);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
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

  [[nodiscard]] result describe_type(guid id, type_info& output) const noexcept try {
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

  [[nodiscard]] result describe_type(engine::type_id id, type_info& output) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return result::not_ready;
    const auto found = by_cpp_type_.find(id);
    if (found == by_cpp_type_.end())
      return result::not_found;
    output = describe_record(*found->second);
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

  [[nodiscard]] result describe_property(guid id, property_id property,
                                         property_info& output) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return result::not_ready;
    const auto* record = find_type_unlocked(id);
    if (!record)
      return result::not_found;
    const auto found = std::ranges::find(record->properties, property.get(), &property_record::id);
    if (found == record->properties.end())
      return result::not_found;
    output = describe_record(*found);
    return result::success;
  } catch (...) {
    return result::internal;
  }

  result find_serialization(engine::type_id id, serialization_binding& output) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return result::not_ready;
    const auto found = by_cpp_type_.find(id);
    if (found == by_cpp_type_.end())
      return result::not_found;
    const auto& codec = found->second->serialization;
    if (!codec.encode && !codec.decode)
      return result::unsupported;
    output = codec;
    return result::success;
  } catch (...) {
    return result::internal;
  }

private:
  [[nodiscard]] gneiss::engine::result find_accessor(guid type_id, std::uint32_t property_id,
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
      const auto found =
          std::ranges::find_if(type->properties, [property_id](const property_record& property) {
            return property.id == property_id;
          });
      if (found == type->properties.end()) {
        return gneiss::engine::result::not_found;
      }
      if (found->accessor.kind == gneiss::engine::value_kind::invalid) {
        return gneiss::engine::result::unsupported;
      }
      output = found->accessor;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  [[nodiscard]] type_record* find_type_unlocked(guid id) noexcept {
    const auto found =
        std::ranges::find_if(types_, [id](const auto& type) { return type->guid == id; });
    return found == types_.end() ? nullptr : found->get();
  }

  [[nodiscard]] const type_record* find_type_unlocked(guid id) const noexcept {
    const auto found =
        std::ranges::find_if(types_, [id](const auto& type) { return type->guid == id; });
    return found == types_.end() ? nullptr : found->get();
  }

  mutable std::shared_mutex mutex_;
  std::vector<std::unique_ptr<type_record>> types_;
  std::map<engine::type_id, type_record*> by_cpp_type_;
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
result register_type(const registry_ref& state, const type_definition& desc,
                     std::span<const property_binding> bindings) noexcept {
  return state ? state->register_type(desc, bindings) : result::invalid_handle;
}

gneiss::engine::result bind_property(const registry_ref& state, guid type_id, property_id property,
                                     const property_binding& desc) noexcept {
  return state ? state->bind_property(type_id, property.get(), desc)
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
gneiss::engine::result get_property(const registry_ref& state, guid type_id,
                                    std::uint32_t property_id, const void* object,
                                    gneiss::engine::value& output) noexcept {
  return state ? state->get_property(type_id, property_id, object, output)
               : gneiss::engine::result::invalid_handle;
}
gneiss::engine::result set_property(const registry_ref& state, guid type_id,
                                    std::uint32_t property_id, void* object,
                                    const gneiss::engine::value& value) noexcept {
  return state ? state->set_property(type_id, property_id, object, value)
               : gneiss::engine::result::invalid_handle;
}

} // namespace gneiss::engine::meta

namespace gneiss::engine::meta {
result describe_type(const registry_ref& registry, guid id, type_info& output) noexcept {
  return registry ? registry->describe_type(id, output) : result::invalid_handle;
}
result describe_type(const registry_ref& registry, engine::type_id id, type_info& output) noexcept {
  return registry ? registry->describe_type(id, output) : result::invalid_handle;
}
result describe_type_at(const registry_ref& registry, std::uint32_t index,
                        type_info& output) noexcept {
  return registry ? registry->describe_type_at(index, output) : result::invalid_handle;
}
result describe_property(const registry_ref& registry, guid type, property_id property,
                         property_info& output) noexcept {
  return registry ? registry->describe_property(type, property, output) : result::invalid_handle;
}

result read_property(const registry_ref& registry, guid type, property_id property,
                     const void* object, gneiss::engine::value& output) noexcept {
  gneiss::engine::value pending;
  const auto operation = get_property(registry, type, property.get(), object, pending);
  if (operation.failed())
    return operation;
  output = std::move(pending);
  return result::success;
}
result write_property(const registry_ref& registry, guid type, property_id property, void* object,
                      const gneiss::engine::value& input) noexcept {
  return set_property(registry, type, property.get(), object, input);
}
} // namespace gneiss::engine::meta

namespace gneiss::engine::meta {
result find_serialization(const registry_ref& registry, type_id id,
                          serialization_binding& output) noexcept {
  return registry ? registry->find_serialization(id, output) : result::invalid_handle;
}
}
