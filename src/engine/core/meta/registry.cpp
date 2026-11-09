// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/meta/registry.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/detail/type_ops.hpp"
#include "engine/core/utf8.hpp"

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
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gneiss::engine::meta {
namespace detail {
struct type_factory {
  static meta::type make(registry_ref registry, const type_ops* access, engine::type_id id,
                         engine::guid guid, type_kind kind, value_kind scalar, bool enumeration) {
    meta::type out;
    out.registry_ = std::move(registry);
    out.access_ = access;
    out.id_ = id;
    if (!id && kind == type_kind::scalar && !enumeration) {
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((is_scalar_value_kind(value::kind_index[I + 1]) && scalar == value::kind_index[I + 1]
              ? (void)(out.id_ = runtime_type_of<
                           std::variant_alternative_t<I + 1, value::storage_type>>())
              : (void)0),
         ...);
      }(std::make_index_sequence<std::variant_size_v<value::storage_type> - 1>{});
    }
    out.guid_ = guid;
    out.kind_ = kind;
    out.scalar_ = scalar;
    out.enumeration_ = enumeration;
    return out;
  }
  static meta::type make(registry_ref registry, const type_ops& access) {
    return make(std::move(registry), &access, access.id, access.guid, access.kind(),
                access.scalar_kind(), access.enumeration);
  }
};
}

namespace {

[[nodiscard]] bool is_valid_name(std::string_view text) noexcept {
  return text.find('\0') == std::string_view::npos && utf8::is_valid(text);
}

[[nodiscard]] bool is_valid_property_kind(value_kind kind) noexcept {
  return is_scalar_value_kind(kind);
}

[[nodiscard]] bool is_valid_property_value(const value& value) noexcept {
  if (!is_scalar_value_kind(value.kind()))
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
        else if constexpr (std::is_same_v<type, math::vector2>)
          return std::isfinite(item.x) && std::isfinite(item.y);
        else if constexpr (std::is_same_v<type, math::vector3>)
          return std::isfinite(item.x) && std::isfinite(item.y) && std::isfinite(item.z);
        else if constexpr (std::is_same_v<type, math::quaternion> ||
                           std::is_same_v<type, math::vector4>)
          return std::isfinite(item.x) && std::isfinite(item.y) && std::isfinite(item.z) &&
                 std::isfinite(item.w);
        else if constexpr (std::is_same_v<type, math::matrix3> ||
                           std::is_same_v<type, math::matrix4>)
          return std::ranges::all_of(math::components(item),
                                     [](float v) { return std::isfinite(v); });
        else
          return true;
      },
      value.payload);
}

using property_accessor = property_binding;
bool valid_binding(const property_binding& binding) noexcept {
  if (binding.enum_type && binding.kind != value_kind::int64 && binding.kind != value_kind::uint64)
    return false;
  if (binding.object_type)
    return binding.kind == value_kind::invalid && !binding.getter && !binding.setter &&
           binding.object_getter &&
           (static_cast<bool>(binding.object_mutator) ==
            static_cast<bool>(binding.object_assign)) &&
           (static_cast<bool>(binding.object_setter) ==
            static_cast<bool>(binding.object_construct)) &&
           (!binding.object_setter || (!binding.object_mutator && binding.access));
  return is_valid_property_kind(binding.kind) && (binding.getter || binding.setter) &&
         !binding.object_getter && !binding.object_mutator && !binding.object_assign &&
         !binding.object_setter && !binding.object_construct &&
         (!binding.access || (binding.access->kind() == type_kind::scalar &&
                              binding.access->scalar_kind() == binding.kind &&
                              binding.access->enumeration == bool(binding.enum_type) &&
                              (!binding.enum_type || binding.access->id == binding.enum_type)));
}

struct property_record {
  std::uint32_t id = 0U;
  engine::guid type_guid{};
  property_access access = property_access::read_write;
  std::string name;
  property_accessor accessor{};
  engine::guid declaring_type{};
  engine::type_id declaring_id{};
  meta::metadata extensions{};
};

struct type_record {
  engine::guid guid{};
  engine::type_id type_id{};
  std::uint32_t schema_version = 0;
  std::string name;
  std::vector<property_record> properties;
  std::vector<std::size_t> property_names;
  object_ops object{};
  meta::metadata extensions{};
};

[[nodiscard]] bool validate_property(const property_definition& property) noexcept {
  return property.id.is_valid() &&
         (property.access == property_access::read_write ||
          property.access == property_access::read_only) &&
         !property.name.empty() &&
         property.name.size() <= std::numeric_limits<std::uint32_t>::max() &&
         is_valid_name(property.name);
}
[[nodiscard]] bool validate_type(const type_definition& desc) noexcept {
  if ((!desc.guid && !desc.type_id) || desc.schema_version == 0U || desc.name.empty() ||
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
[[nodiscard]] property_capabilities record_capabilities(const property_record& property) noexcept {
  return ((property.accessor.getter || property.accessor.object_getter)
              ? property_capabilities::readable
              : property_capabilities::none) |
         ((property.accessor.setter || property.accessor.object_mutator ||
           property.accessor.object_setter)
              ? property_capabilities::writable
              : property_capabilities::none);
}
[[nodiscard]] meta::type record_type(const registry_ref& registry,
                                     const property_record& property) noexcept {
  return property.accessor.access
             ? detail::type_factory::make(registry, *property.accessor.access)
             : detail::type_factory::make(
                   registry, nullptr,
                   property.accessor.enum_type ? property.accessor.enum_type
                                               : property.accessor.object_type,
                   property.type_guid,
                   property.accessor.object_type                   ? type_kind::object
                   : property.accessor.kind != value_kind::invalid ? type_kind::scalar
                                                                   : type_kind::invalid,
                   property.accessor.kind, bool(property.accessor.enum_type));
}

} // namespace

bool valid_scalar(const value& input) noexcept { return is_valid_property_value(input); }

class type_registry_state final : public std::enable_shared_from_this<type_registry_state> {
public:
  error register_enum(engine::type_id id, value_kind kind,
                      std::span<const enum_entry> entries) noexcept try {
    if (!id || entries.empty() || (kind != value_kind::int64 && kind != value_kind::uint64))
      return error::invalid_argument;
    for (std::size_t i = 0; i < entries.size(); ++i) {
      if (!is_valid_name(entries[i].name) || entries[i].data.kind() != kind)
        return error::invalid_argument;
      for (std::size_t j = 0; j < i; ++j)
        if (entries[j].name == entries[i].name || entries[j].data == entries[i].data)
          return error::invalid_argument;
    }
    std::unique_lock lock(mutex_);
    if (is_frozen_)
      return error::invalid_state;
    if (enums_.contains(id))
      return error::invalid_argument;
    enums_.emplace(id, enum_record{kind, {entries.begin(), entries.end()}});
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }

  [[nodiscard]] error register_type(const type_definition& desc,
                                    std::span<const property_binding> bindings = {}) noexcept {
    if (!validate_type(desc)) {
      return error::invalid_argument;
    }
    for (std::size_t i = 0; i < desc.properties.size(); ++i) {
      if (!desc.properties[i].type_guid &&
          (bindings.size() != desc.properties.size() || !bindings[i].access ||
           (bindings[i].access->kind() != type_kind::object &&
            bindings[i].access->kind() != type_kind::sequence &&
            bindings[i].access->kind() != type_kind::associative &&
            bindings[i].access->kind() != type_kind::optional &&
            bindings[i].access->kind() != type_kind::reference &&
            bindings[i].access->kind() != type_kind::variant)))
        return error::invalid_argument;
    }
    if (!bindings.empty()) {
      if (bindings.size() != desc.properties.size())
        return error::invalid_argument;
      for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        if (!valid_binding(binding) ||
            (desc.properties[i].access == property_access::read_only &&
             (binding.setter || binding.object_mutator || binding.object_setter)))
          return error::invalid_argument;
      }
    }
    try {
      std::unique_lock lock(mutex_);
      if (is_frozen_) {
        return error::invalid_state;
      }
      if ((desc.guid && find_type_unlocked(desc.guid)) ||
          (desc.type_id && by_cpp_type_.contains(desc.type_id))) {
        return error::invalid_argument;
      }

      auto record = std::make_unique<type_record>();
      record->guid = desc.guid;
      record->type_id = desc.type_id;
      record->schema_version = desc.schema_version;
      record->name.assign(desc.name);
      record->object = desc.object;
      record->extensions = desc.extensions;
      record->properties.reserve(desc.properties.size());
      for (const auto& property : desc.properties) {
        record->properties.push_back({.id = property.id.get(),
                                      .type_guid = property.type_guid,
                                      .access = property.access,
                                      .name = std::string(property.name),
                                      .accessor = {},
                                      .declaring_type = desc.guid,
                                      .declaring_id = desc.type_id,
                                      .extensions = property.extensions});
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
      if (found->access == property_access::read_only &&
          (desc.setter != nullptr || desc.object_mutator != nullptr ||
           desc.object_setter != nullptr)) {
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
                       current.object_setter == requested.object_setter &&
                       current.object_construct == requested.object_construct &&
                       current.access == requested.access &&
                       current.enum_type == requested.enum_type
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
      // 在临时表中合并属性；冻结失败时保留原始注册内容，可补注册后重试。
      std::vector<std::vector<property_record>> inherited;
      inherited.reserve(types_.size());
      for (const auto& type : types_) {
        auto properties = type->properties;
        auto current = type.get();
        std::set<engine::type_id> chain;
        while (current->object.base_type) {
          if (!chain.insert(current->type_id).second || !current->object.upcast)
            return error::invalid_argument;
          auto base = by_cpp_type_.find(current->object.base_type);
          if (base == by_cpp_type_.end())
            return error::not_found;
          current = base->second;
          for (const auto& property : current->properties) {
            if (std::ranges::any_of(properties, [&](const auto& existing) {
                  return existing.id == property.id || existing.name == property.name;
                }))
              return error::invalid_argument;
            properties.push_back(property);
          }
        }
        inherited.push_back(std::move(properties));
        for (const auto& property : type->properties) {
          if (!property.accessor.object_type)
            continue;
          if (property.accessor.access) {
            const auto* access = property.accessor.access;
            if (access->id != property.accessor.object_type || access->guid != property.type_guid)
              return error::invalid_argument;
            auto status = detail::visit_type_leaves(*access, [&](const detail::type_ops& leaf) {
              if (leaf.kind() != type_kind::object)
                return error::success;
              const auto found = by_cpp_type_.find(leaf.id);
              if (found == by_cpp_type_.end())
                return error::not_found;
              return found->second->guid == leaf.guid ? error::success : error::invalid_argument;
            });
            if (status.failed())
              return status;
            continue;
          }
          const auto found = by_cpp_type_.find(property.accessor.object_type);
          if (found == by_cpp_type_.end())
            return error::not_found;
          if (found->second->guid != property.type_guid)
            return error::invalid_argument;
        }
      }
      for (auto& type : types_) {
        for (auto& property : type->properties) {
          const auto found = enums_.find(property.accessor.enum_type);
          if (found == enums_.end())
            continue;
          if (found->second.kind != property.accessor.kind)
            return error::invalid_argument;
        }
      }
      // 名称索引使用排序后的槽位；所有分配均在提交冻结状态前完成。
      std::vector<std::vector<std::size_t>> names;
      names.reserve(inherited.size());
      for (auto& properties : inherited) {
        std::ranges::sort(properties, {}, &property_record::id);
        std::vector<std::size_t> indices(properties.size());
        for (std::size_t i = 0; i < indices.size(); ++i)
          indices[i] = i;
        std::ranges::sort(indices, [&](std::size_t left, std::size_t right) {
          return properties[left].name < properties[right].name;
        });
        names.push_back(std::move(indices));
      }
      for (std::size_t i = 0; i < types_.size(); ++i) {
        types_[i]->properties.swap(inherited[i]);
        types_[i]->property_names.swap(names[i]);
      }
      std::ranges::sort(types_, [](const auto& left, const auto& right) {
        if (left->guid != right->guid)
          return left->guid.bytes() < right->guid.bytes();
        return left->type_id < right->type_id;
      });
      is_frozen_ = true;
      return error::success;
    } catch (...) {
      return error::internal;
    }
  }

  template <class Key>
  [[nodiscard]] result<value> get_property(Key type_id, std::uint32_t property_id,
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
      auto target = property_target(type_id, property_id, const_cast<void*>(object));
      if (!target)
        return target.error();
      result = accessor->getter(accessor->user_data, *target, output);
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

  template <class Key>
  [[nodiscard]] error set_property(Key type_id, std::uint32_t property_id, void* object,
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
      auto target = property_target(type_id, property_id, object);
      if (!target)
        return target.error();
      return accessor->setter(accessor->user_data, *target, value);
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

  result<meta::type> resolve_access(const detail::type_ops& access) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    auto status = detail::visit_type_leaves(access, [&](const detail::type_ops& leaf) {
      if (leaf.enumeration && !enums_.contains(leaf.id))
        return error::not_found;
      if (leaf.kind() == type_kind::object && !by_cpp_type_.contains(leaf.id))
        return error::not_found;
      return error::success;
    });
    if (status.failed())
      return status;
    if (access.kind() == type_kind::object) {
      const auto& record = *by_cpp_type_.at(access.id);
      return detail::type_factory::make(owner(), &access, record.type_id, record.guid,
                                        type_kind::object, value_kind::invalid, false);
    }
    return detail::type_factory::make(owner(), access);
  } catch (...) {
    return error::internal;
  }

  template <class Key> result<meta::type> lookup_type(Key key) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    if (!key)
      return error::not_found;
    const type_record* record = nullptr;
    if constexpr (std::is_same_v<Key, engine::guid>) {
      record = find_type_unlocked(key);
    } else {
      const auto found = by_cpp_type_.find(key);
      if (found != by_cpp_type_.end())
        record = found->second;
    }
    if (record)
      return detail::type_factory::make(owner(), nullptr, record->type_id, record->guid,
                                        type_kind::object, value_kind::invalid, false);
    if constexpr (std::is_same_v<Key, engine::type_id>) {
      auto entry = enums_.find(key);
      if (entry != enums_.end())
        return detail::type_factory::make(owner(), nullptr, key, {}, type_kind::scalar,
                                          entry->second.kind, true);
    }
    const detail::type_ops* found = nullptr;
    auto match = [&](const detail::type_ops& access) {
      if constexpr (std::is_same_v<Key, engine::guid>)
        return !access.enumeration && access.guid == key;
      else
        return access.id == key;
    };
    // 基础值使用既有 value 类型集合，不另建可变注册表。
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      (([&] {
         if constexpr (is_scalar_value_kind(value::kind_index[I + 1])) {
           const auto& access =
               detail::ops_of<std::variant_alternative_t<I + 1, value::storage_type>>();
           if (match(access))
             found = &access;
         }
       }()),
       ...);
    }(std::make_index_sequence<std::variant_size_v<value::storage_type> - 1>{});
    if (found)
      return detail::type_factory::make(owner(), *found);
    for (const auto& record : types_) {
      for (const auto& property : record->properties) {
        const auto* access = property.accessor.access;
        for (std::size_t depth = 0; access && depth <= 64; ++depth) {
          if (match(*access)) {
            if (access->enumeration && !enums_.contains(access->id))
              return error::not_found;
            return detail::type_factory::make(owner(), *access);
          }
          access = access->element_ops() ? &access->element() : nullptr;
        }
      }
    }
    return error::not_found;
  } catch (...) {
    return error::internal;
  }

  result<std::span<const enum_entry>> enum_entries(engine::type_id id) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    const auto found = enums_.find(id);
    if (found == enums_.end())
      return error::not_found;
    return std::span<const enum_entry>{found->second.entries};
  } catch (...) {
    return error::internal;
  }
  result<meta::type> type_at(std::uint32_t index) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    if (index >= types_.size())
      return error::not_found;
    const auto& record = *types_[index];
    return detail::type_factory::make(owner(), nullptr, record.type_id, record.guid,
                                      type_kind::object, value_kind::invalid, false);
  } catch (...) {
    return error::internal;
  }
  template <class Key> std::uint32_t schema_version(Key id) const noexcept try {
    std::shared_lock lock(mutex_);
    const auto* record = is_frozen_ ? find_type_unlocked(id) : nullptr;
    return record ? record->schema_version : 0;
  } catch (...) {
    return 0;
  }

  template <class Key> const void* extension(Key id, engine::type_id key) const noexcept try {
    std::shared_lock lock(mutex_);
    const auto* record = is_frozen_ ? find_type_unlocked(id) : nullptr;
    return record ? record->extensions.find(key) : nullptr;
  } catch (...) {
    return nullptr;
  }

  template <class Key> std::string_view type_name(Key id) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return {};
    const auto* record = find_type_unlocked(id);
    return record ? std::string_view{record->name} : std::string_view{};
  } catch (...) {
    return {};
  }

  template <class Owner, class Key>
  result<property> lookup_property(Owner id, Key key) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    const auto* record = find_type_unlocked(id);
    if (!record)
      return error::not_found;
    const property_record* found = nullptr;
    if constexpr (std::is_same_v<Key, property_id>) {
      found = find_property_unlocked(*record, key.get());
    } else {
      const auto match = std::ranges::lower_bound(
          record->property_names, key, {},
          [&](std::size_t index) -> std::string_view { return record->properties[index].name; });
      if (match != record->property_names.end() && record->properties[*match].name == key)
        found = &record->properties[*match];
    }
    if (!found)
      return error::not_found;
    return property_range{owner(), found, 1}[0];
  } catch (...) {
    return error::internal;
  }

  template <class Key> result<property_range> properties(Key id) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    const auto* record = find_type_unlocked(id);
    if (!record)
      return error::not_found;
    return property_range{owner(), record->properties.data(), record->properties.size()};
  } catch (...) {
    return error::internal;
  }

  result<object_ops> find_object_ops(engine::type_id id) const noexcept try {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    const auto found = by_cpp_type_.find(id);
    if (found == by_cpp_type_.end())
      return error::not_found;
    return found->second->object;
  } catch (...) {
    return error::internal;
  }

  template <class Key>
  result<property_binding> find_object_property(Key type, property_id id) const noexcept try {
    auto binding = find_accessor(type, id.get());
    if (!binding)
      return binding.error();
    if (!binding->object_type)
      return error::unsupported;
    return *binding;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }

private:
  template <class Key>
  result<void*> property_target(Key id, std::uint32_t property, void* object) const {
    std::shared_lock lock(mutex_);
    const auto* current = find_type_unlocked(id);
    if (!is_frozen_ || !current)
      return error::invalid_state;
    const auto* found = find_property_unlocked(*current, property);
    if (!found)
      return error::not_found;
    const auto owner = found->declaring_type;
    while (found->declaring_id ? current->type_id != found->declaring_id : current->guid != owner) {
      object = current->object.upcast(object);
      current = by_cpp_type_.at(current->object.base_type);
    }
    return object;
  }
  registry_ref owner() const {
    return std::const_pointer_cast<type_registry_state>(shared_from_this());
  }
  template <class Key>
  [[nodiscard]] result<property_accessor> find_accessor(Key type_id,
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
      const auto* found = find_property_unlocked(*type, property_id);
      if (!found)
        return error::not_found;
      if (found->accessor.kind == value_kind::invalid && !found->accessor.object_type) {
        return error::unsupported;
      }
      return found->accessor;
    } catch (...) {
      return error::internal;
    }
  }

  static const property_record* find_property_unlocked(const type_record& type,
                                                       std::uint32_t id) noexcept {
    const auto found = std::ranges::lower_bound(type.properties, id, {}, &property_record::id);
    return found != type.properties.end() && found->id == id ? &*found : nullptr;
  }
  [[nodiscard]] const type_record* find_type_unlocked(engine::type_id id) const noexcept {
    const auto found = by_cpp_type_.find(id);
    return found == by_cpp_type_.end() ? nullptr : found->second;
  }
  [[nodiscard]] type_record* find_type_unlocked(guid id) noexcept {
    return const_cast<type_record*>(std::as_const(*this).find_type_unlocked(id));
  }
  [[nodiscard]] const type_record* find_type_unlocked(guid id) const noexcept {
    if (!id)
      return nullptr;
    if (is_frozen_) {
      const auto found = std::ranges::lower_bound(
          types_, id.bytes(), {}, [](const auto& type) { return type->guid.bytes(); });
      return found != types_.end() && (*found)->guid == id ? found->get() : nullptr;
    }
    const auto found =
        std::ranges::find_if(types_, [id](const auto& type) { return type->guid == id; });
    return found == types_.end() ? nullptr : found->get();
  }

  mutable std::shared_mutex mutex_;
  std::vector<std::unique_ptr<type_record>> types_;
  std::map<engine::type_id, type_record*> by_cpp_type_;
  struct enum_record {
    value_kind kind;
    std::vector<enum_entry> entries;
  };
  std::map<engine::type_id, enum_record> enums_;
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

result<property> type::find_property(property_id id) const noexcept {
  if (kind_ == type_kind::invalid)
    return error::invalid_state;
  if (kind_ != type_kind::object)
    return error::unsupported;
  return id_ ? registry_->lookup_property(id_, id) : registry_->lookup_property(guid_, id);
}
result<property> type::find_property(std::string_view name) const noexcept {
  if (kind_ == type_kind::invalid)
    return error::invalid_state;
  if (kind_ != type_kind::object)
    return error::unsupported;
  return id_ ? registry_->lookup_property(id_, name) : registry_->lookup_property(guid_, name);
}
result<property_range> type::properties() const noexcept {
  if (kind_ == type_kind::invalid)
    return error::invalid_state;
  if (kind_ != type_kind::object)
    return error::unsupported;
  return id_ ? registry_->properties(id_) : registry_->properties(guid_);
}
const void* type::extension(engine::type_id key) const noexcept {
  return registry_ ? (id_ ? registry_->extension(id_, key) : registry_->extension(guid_, key))
                   : nullptr;
}
const void* property::extension(engine::type_id key) const noexcept {
  return record_ ? static_cast<const property_record*>(record_)->extensions.find(key) : nullptr;
}
std::uint32_t type::schema_version() const noexcept {
  return registry_ ? (id_ ? registry_->schema_version(id_) : registry_->schema_version(guid())) : 0;
}
std::string_view type::name() const noexcept {
  return registry_ ? (id_ ? registry_->type_name(id_) : registry_->type_name(guid()))
                   : std::string_view{};
}
result<meta::type> find_type(const registry_ref& registry, engine::type_id id) noexcept {
  return registry ? registry->lookup_type(id) : result<meta::type>{error::invalid_handle};
}
result<meta::type> find_type(const registry_ref& registry, engine::guid id) noexcept {
  return registry ? registry->lookup_type(id) : result<meta::type>{error::invalid_handle};
}
namespace detail {
result<meta::type> resolve_type(const registry_ref& registry, const type_ops& access) noexcept {
  return registry ? registry->resolve_access(access) : result<meta::type>{error::invalid_handle};
}
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
result<std::span<const enum_entry>> enum_entries(const registry_ref& state,
                                                 engine::type_id id) noexcept {
  return state ? state->enum_entries(id)
               : result<std::span<const enum_entry>>{error::invalid_handle};
}
error register_enum(const registry_ref& state, engine::type_id id, value_kind kind,
                    std::span<const enum_entry> entries) noexcept {
  return state ? state->register_enum(id, kind, entries) : error::invalid_handle;
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
result<meta::type> type_at(const registry_ref& registry, std::uint32_t index) noexcept {
  return registry ? registry->type_at(index) : result<meta::type>{error::invalid_handle};
}
result<property> find_property(const registry_ref& registry, guid id, property_id field) noexcept {
  return registry ? registry->lookup_property(id, field) : result<property>{error::invalid_handle};
}
result<value> read_property(const registry_ref& registry, engine::type_id type,
                            property_id property, const void* object) noexcept {
  return registry ? registry->get_property(type, property.get(), object) : error::invalid_handle;
}
error write_property(const registry_ref& registry, engine::type_id type, property_id property,
                     void* object, const value& input) noexcept {
  return registry ? registry->set_property(type, property.get(), object, input)
                  : error::invalid_handle;
}
result<property_binding> find_object_property(const registry_ref& registry, engine::type_id type,
                                              property_id id) noexcept {
  return registry ? registry->find_object_property(type, id)
                  : result<property_binding>{error::invalid_handle};
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
result<object_ops> find_object_ops(const registry_ref& registry, type_id id) noexcept {
  return registry ? registry->find_object_ops(id) : error::invalid_handle;
}
result<property_binding> find_object_property(const registry_ref& registry, guid type,
                                              property_id id) noexcept {
  return registry ? registry->find_object_property(type, id)
                  : result<property_binding>{error::invalid_handle};
}
result<property_range> properties(const registry_ref& registry, engine::guid id) noexcept {
  return registry ? registry->properties(id) : result<property_range>{error::invalid_argument};
}
property property_range::iterator::operator*() const noexcept {
  return property{registry_, static_cast<const property_record*>(records_) + index_};
}
property_id property::id() const noexcept {
  return record_ ? property_id{static_cast<const property_record*>(record_)->id} : property_id{};
}
std::string_view property::name() const noexcept {
  return record_ ? std::string_view{static_cast<const property_record*>(record_)->name}
                 : std::string_view{};
}
engine::type_id property::declaring_type_id() const noexcept {
  return record_ ? static_cast<const property_record*>(record_)->declaring_id : engine::type_id{};
}
engine::guid property::declaring_type() const noexcept {
  return record_ ? static_cast<const property_record*>(record_)->declaring_type : engine::guid{};
}
meta::type property::type() const noexcept {
  return record_ ? record_type(registry_, *static_cast<const property_record*>(record_))
                 : meta::type{};
}

property_capabilities property::capabilities() const noexcept {
  return record_ ? record_capabilities(*static_cast<const property_record*>(record_))
                 : property_capabilities::none;
}
}

namespace gneiss::engine::meta {
property property_range::operator[](std::size_t index) const noexcept {
  return property{registry_, static_cast<const property_record*>(records_) + index};
}
}
