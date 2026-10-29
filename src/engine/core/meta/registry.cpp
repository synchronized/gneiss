// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/meta/registry.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/detail/type_access.hpp"

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

[[nodiscard]] bool is_valid_property_kind(value_kind kind) noexcept {
  return kind >= value_kind::boolean && kind <= value_kind::vec4;
}

[[nodiscard]] bool is_valid_property_value(const value& value) noexcept {
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
        else if constexpr (std::is_same_v<type, math::vector3>)
          return std::isfinite(item.x) && std::isfinite(item.y) && std::isfinite(item.z);
        else if constexpr (std::is_same_v<type, math::quaternion> ||
                           std::is_same_v<type, math::vector4>)
          return std::isfinite(item.x) && std::isfinite(item.y) && std::isfinite(item.z) &&
                 std::isfinite(item.w);
        else
          return true;
      },
      value.payload);
}

using property_accessor = property_binding;
bool valid_binding(const property_binding& binding) noexcept {
  if (binding.object_type)
    return binding.kind == value_kind::invalid && !binding.getter && !binding.setter &&
           binding.object_getter &&
           (static_cast<bool>(binding.object_mutator) == static_cast<bool>(binding.object_assign));
  return is_valid_property_kind(binding.kind) && (binding.getter || binding.setter) &&
         !binding.object_getter && !binding.object_mutator && !binding.object_assign &&
         !binding.access;
}

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
  return property.id.is_valid() &&
         !has_flags(property.flags, property_flags::read_only | property_flags::required) &&
         (static_cast<std::uint32_t>(property.flags) &
          ~static_cast<std::uint32_t>(property_flags::read_only | property_flags::required)) ==
             0U &&
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
          .capabilities = ((property.accessor.getter || property.accessor.object_getter)
                               ? property_capabilities::readable
                               : property_capabilities::none) |
                          ((property.accessor.setter || property.accessor.object_mutator)
                               ? property_capabilities::writable
                               : property_capabilities::none),
          .object_type = property.accessor.object_type,
          .category = property.accessor.access        ? property.accessor.access->kind
                      : property.accessor.object_type ? type_kind::object
                      : property.accessor.kind != value_kind::invalid ? type_kind::scalar
                                                                      : type_kind::invalid};
}

[[nodiscard]] type_info describe_record(const type_record& record) {
  type_info pending{.guid = record.guid,
                    .type_id = record.type_id,
                    .schema_version = record.schema_version,
                    .name = record.name,
                    .properties = {}};
  pending.properties.reserve(record.properties.size());
  for (const auto& property : record.properties)
    pending.properties.push_back(describe_record(property));
  return pending;
}

} // namespace

bool valid_scalar(const value& input) noexcept { return is_valid_property_value(input); }

class type_registry_state final {
public:
  [[nodiscard]] error register_type(const type_definition& desc,
                                    std::span<const property_binding> bindings = {}) noexcept {
    if (!validate_type(desc)) {
      return error::invalid_argument;
    }
    for (std::size_t i = 0; i < desc.properties.size(); ++i) {
      if (!desc.properties[i].type_guid &&
          (bindings.size() != desc.properties.size() || !bindings[i].access ||
           (bindings[i].access->kind != type_kind::sequence &&
            bindings[i].access->kind != type_kind::associative &&
            bindings[i].access->kind != type_kind::optional)))
        return error::invalid_argument;
    }
    if (!bindings.empty()) {
      if (bindings.size() != desc.properties.size())
        return error::invalid_argument;
      for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        if (!valid_binding(binding) ||
            (has_flags(desc.properties[i].flags, property_flags::read_only) &&
             (binding.setter || binding.object_mutator)))
          return error::invalid_argument;
      }
    }
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return error::invalid_state;
      }
      if (find_type_unlocked(desc.guid) || (desc.type_id && by_cpp_type_.contains(desc.type_id))) {
        return error::invalid_argument;
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
        record->properties[i].accessor = binding;
      }
      // 先预留所有者数组容量，索引插入成功后移动所有权不再分配；失败不会留下半条记录。
      types_.reserve(types_.size() + 1);
      if (desc.type_id)
        by_cpp_type_.emplace(desc.type_id, record.get());
      types_.push_back(std::move(record));
      return error::success;
    } catch (const std::bad_alloc&) {
      return error::out_of_memory;
    } catch (...) {
      return error::internal;
    }
  }

  [[nodiscard]] error bind_property(guid type_id, std::uint32_t property_id,
                                    const property_binding& desc) noexcept {
    if (!valid_binding(desc)) {
      return error::invalid_argument;
    }
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return error::invalid_state;
      }
      auto* type = find_type_unlocked(type_id);
      if (type == nullptr) {
        return error::not_found;
      }
      auto found =
          std::ranges::find_if(type->properties, [property_id](const property_record& property) {
            return property.id == property_id;
          });
      if (found == type->properties.end()) {
        return error::not_found;
      }
      if ((found->flags & static_cast<std::uint32_t>(property_flags::read_only)) != 0U &&
          (desc.setter != nullptr || desc.object_mutator != nullptr)) {
        return error::invalid_argument;
      }
      const auto& requested = desc;
      if (found->accessor.kind != value_kind::invalid || found->accessor.object_type) {
        const auto& current = found->accessor;
        return current.kind == requested.kind && current.getter == requested.getter &&
                       current.setter == requested.setter &&
                       current.user_data == requested.user_data &&
                       current.object_type == requested.object_type &&
                       current.object_getter == requested.object_getter &&
                       current.object_mutator == requested.object_mutator &&
                       current.object_assign == requested.object_assign &&
                       current.access == requested.access
                   ? error::success
                   : error::invalid_argument;
      }
      found->accessor = requested;
      return error::success;
    } catch (...) {
      return error::internal;
    }
  }

  [[nodiscard]] error freeze() noexcept {
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return error::success;
      }
      for (const auto& type : types_) {
        for (const auto& property : type->properties) {
          if (!property.accessor.object_type)
            continue;
          if (property.accessor.access) {
            const auto* access = property.accessor.access;
            if (access->id != property.accessor.object_type || access->guid != property.type_guid)
              return error::invalid_argument;
            std::size_t depth = 0;
            while (access->element) {
              if (++depth > 64)
                return error::invalid_argument;
              access = &access->element();
            }
            if (access->kind != type_kind::object)
              continue;
            const auto leaf = by_cpp_type_.find(access->id);
            if (leaf == by_cpp_type_.end())
              return error::not_found;
            if (leaf->second->guid != access->guid)
              return error::invalid_argument;
            continue;
          }
          const auto found = by_cpp_type_.find(property.accessor.object_type);
          if (found == by_cpp_type_.end())
            return error::not_found;
          if (found->second->guid != property.type_guid)
            return error::invalid_argument;
        }
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
      return error::success;
    } catch (...) {
      return error::internal;
    }
  }

  [[nodiscard]] result<value> get_property(guid type_id, std::uint32_t property_id,
                                           const void* object) const noexcept {
    if (!object)
      return error::invalid_argument;
    auto accessor = find_accessor(type_id, property_id);
    if (!accessor)
      return accessor.error();
    if (accessor->getter == nullptr) {
      return error::unsupported;
    }
    value output;
    error result = error::internal;
    try {
      result = accessor->getter(accessor->user_data, object, output);
    } catch (const std::bad_alloc&) {
      return error::out_of_memory;
    } catch (...) {
      return error::internal;
    }
    if (result != error::success) {
      return result;
    }
    if (output.kind() != accessor->kind || !is_valid_property_value(output)) {
      return error::internal;
    }
    return output;
  }

  [[nodiscard]] error set_property(guid type_id, std::uint32_t property_id, void* object,
                                   const value& value) const noexcept {
    if (!is_valid_property_value(value)) {
      return error::invalid_argument;
    }
    if (!object)
      return error::invalid_argument;
    auto accessor = find_accessor(type_id, property_id);
    if (!accessor)
      return accessor.error();
    if (accessor->setter == nullptr) {
      return error::unsupported;
    }
    if (value.kind() != accessor->kind) {
      return error::invalid_argument;
    }
    try {
      return accessor->setter(accessor->user_data, object, value);
    } catch (const std::bad_alloc&) {
      return error::out_of_memory;
    } catch (...) {
      return error::internal;
    }
  }

  [[nodiscard]] result<bool> is_frozen() const noexcept {
    try {
      std::shared_lock lock(mutex_);
      return is_frozen_;
    } catch (...) {
      return error::internal;
    }
  }

  [[nodiscard]] result<std::uint32_t> type_count() const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return error::not_ready;
      }
      return static_cast<std::uint32_t>(types_.size());
    } catch (...) {
      return error::internal;
    }
  }

  [[nodiscard]] result<type_info> describe_type(guid id) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    const auto* record = find_type_unlocked(id);
    if (!record)
      return error::not_found;
    return describe_record(*record);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }

  [[nodiscard]] result<type_info> describe_type(engine::type_id id) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    const auto found = by_cpp_type_.find(id);
    if (found == by_cpp_type_.end())
      return error::not_found;
    return describe_record(*found->second);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }

  [[nodiscard]] result<type_info> describe_type_at(std::uint32_t index) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    if (index >= types_.size())
      return error::not_found;
    return describe_record(*types_[index]);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }

  [[nodiscard]] result<property_info> describe_property(guid id,
                                                        property_id property) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    const auto* record = find_type_unlocked(id);
    if (!record)
      return error::not_found;
    const auto found = std::ranges::find(record->properties, property.get(), &property_record::id);
    if (found == record->properties.end())
      return error::not_found;
    return describe_record(*found);
  } catch (...) {
    return error::internal;
  }

  result<serialization_binding> find_serialization(engine::type_id id) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    const auto found = by_cpp_type_.find(id);
    if (found == by_cpp_type_.end())
      return error::not_found;
    const auto& codec = found->second->serialization;
    if (!codec.encode && !codec.decode && !codec.reflected_json)
      return error::unsupported;
    return codec;
  } catch (...) {
    return error::internal;
  }

  result<property_binding> find_object_property(guid type, property_id id) const noexcept {
    auto binding = find_accessor(type, id.get());
    if (!binding)
      return binding.error();
    if (!binding->object_type)
      return error::unsupported;
    return *binding;
  }

private:
  [[nodiscard]] result<property_accessor> find_accessor(guid type_id,
                                                        std::uint32_t property_id) const noexcept {
    try {
      std::shared_lock lock(mutex_);
      if (!is_frozen_) {
        return error::not_ready;
      }
      const auto* type = find_type_unlocked(type_id);
      if (type == nullptr) {
        return error::not_found;
      }
      const auto found =
          std::ranges::find_if(type->properties, [property_id](const property_record& property) {
            return property.id == property_id;
          });
      if (found == type->properties.end()) {
        return error::not_found;
      }
      if (found->accessor.kind == value_kind::invalid && !found->accessor.object_type) {
        return error::unsupported;
      }
      return found->accessor;
    } catch (...) {
      return error::internal;
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

result<registry_ref> default_registry() noexcept try {
  static const registry_ref registry = std::make_shared<type_registry_state>();
  return registry;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<registry_ref> create_registry() noexcept {
  try {
    return std::make_shared<type_registry_state>();
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}
error register_type(const registry_ref& state, const type_definition& desc) noexcept {
  return state ? state->register_type(desc) : error::invalid_handle;
}
error register_type(const registry_ref& state, const type_definition& desc,
                    std::span<const property_binding> bindings) noexcept {
  return state ? state->register_type(desc, bindings) : error::invalid_handle;
}

error bind_property(const registry_ref& state, guid type_id, property_id property,
                    const property_binding& desc) noexcept {
  return state ? state->bind_property(type_id, property.get(), desc) : error::invalid_handle;
}
error freeze(const registry_ref& state) noexcept {
  return state ? state->freeze() : error::invalid_handle;
}
result<bool> is_frozen(const registry_ref& state) noexcept {
  return state ? state->is_frozen() : error::invalid_handle;
}
result<std::uint32_t> type_count(const registry_ref& state) noexcept {
  return state ? state->type_count() : error::invalid_handle;
}
result<type_info> describe_type(const registry_ref& registry, guid id) noexcept {
  return registry ? registry->describe_type(id) : error::invalid_handle;
}
result<type_info> describe_type(const registry_ref& registry, engine::type_id id) noexcept {
  return registry ? registry->describe_type(id) : error::invalid_handle;
}
result<type_info> describe_type_at(const registry_ref& registry, std::uint32_t index) noexcept {
  return registry ? registry->describe_type_at(index) : error::invalid_handle;
}
result<property_info> describe_property(const registry_ref& registry, guid type,
                                        property_id property) noexcept {
  return registry ? registry->describe_property(type, property) : error::invalid_handle;
}

result<value> read_property(const registry_ref& registry, guid type, property_id property,
                            const void* object) noexcept {
  return registry ? registry->get_property(type, property.get(), object) : error::invalid_handle;
}

error write_property(const registry_ref& registry, guid type, property_id property, void* object,
                     const value& input) noexcept {
  return registry ? registry->set_property(type, property.get(), object, input)
                  : error::invalid_handle;
}
result<serialization_binding> find_serialization(const registry_ref& registry,
                                                 type_id id) noexcept {
  return registry ? registry->find_serialization(id) : error::invalid_handle;
}
}

namespace gneiss::engine::meta {
result<property_binding> find_object_property(const registry_ref& registry, guid type,
                                              property_id id) noexcept {
  return registry ? registry->find_object_property(type, id)
                  : result<property_binding>{error::invalid_handle};
}
}
