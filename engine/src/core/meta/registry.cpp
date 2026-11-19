// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/base/assert.hpp>
#include <gneiss/engine/core/meta/capabilities/enumeration.hpp>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/ops/type_ops.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/base/string_name.hpp>
#include <gneiss/engine/core/base/utf8.hpp>
#include <gneiss/engine/core/base/error.hpp>

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

namespace gneiss::meta {
namespace detail {
struct property_record {
  std::uint32_t id = 0U;
  gneiss::guid type_guid{};
  property_access access = property_access::read_write;
  string_name name;
  property_binding accessor{};
  gneiss::guid declaring_type{};
  gneiss::type_id declaring_id{};
  meta::metadata extensions{};
};

struct type_record {
  gneiss::guid guid{};
  gneiss::type_id type_id{};
  std::uint32_t schema_version = 0;
  string_name name;
  std::vector<property_record> properties;
  std::vector<std::size_t> property_names;
  object_ops object{};
  meta::metadata extensions{};
  bool enumeration = false;
  value_kind scalar = value_kind::invalid;
  std::vector<enum_entry> entries;
  const type_ops* native{};
};
struct type_factory {
  static meta::type make(registry_ref registry, const type_ops* access,
                         const type_record* record = nullptr) {
    meta::type out;
    out.registry_ = std::move(registry);
    out.access_ = access ? access : record ? record->native : nullptr;
    out.record_ = record;
    return out;
  }
};
}

namespace {
using detail::property_record;
using detail::type_record;

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
      !is_valid_name(desc.name) ||
      (desc.native && (desc.native->id != desc.type_id || desc.native->enumeration))) {
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
                                     const property_record& property) {
  if (property.accessor.access) {
    return detail::resolve_type(registry, *property.accessor.access);
  }
  const auto id =
      property.accessor.enum_type ? property.accessor.enum_type : property.accessor.object_type;
  if (id) {
    return resolve(registry, id);
  }
  meta::type output;
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    (([&] {
       if constexpr (is_scalar_value_kind(value::kind_index[I + 1])) {
         if (property.accessor.kind == value::kind_index[I + 1])
           output = detail::type_factory::make(
               registry, &detail::ops_of<std::variant_alternative_t<I + 1, value::storage_type>>());
       }
     }()),
     ...);
  }(std::make_index_sequence<std::variant_size_v<value::storage_type> - 1>{});
  return output;
}

} // namespace

bool valid_scalar(const value& input) noexcept { return is_valid_property_value(input); }

class type_registry_state final : public std::enable_shared_from_this<type_registry_state> {
public:
  error register_enum(const enum_definition& definition) {
    const auto id = definition.type_id;
    const auto kind = definition.kind;
    const auto entries = definition.entries;
    const auto* native = definition.native;
    if (!definition.name.empty() && !is_valid_name(definition.name))
      return error::invalid_argument;
    if (!id || entries.empty() || (kind != value_kind::int64 && kind != value_kind::uint64))
      return error::invalid_argument;
    if (native && (native->id != id || !native->enumeration || native->scalar_kind() != kind))
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
    if (enums_.contains(id) || by_cpp_type_.contains(id) ||
        (definition.guid &&
         (find_type_unlocked(definition.guid) || find_enum_unlocked(definition.guid))))
      return error::invalid_argument;
    type_record record;
    record.type_id = id;
    record.native = native;
    record.guid = definition.guid;
    record.name = string_name{definition.name};
    record.schema_version = definition.schema_version;
    record.extensions = definition.extensions;
    record.enumeration = true;
    record.scalar = kind;
    record.entries.reserve(entries.size());
    for (const auto& entry : entries)
      record.entries.push_back({string_name{entry.name}.view(), entry.data});
    enums_.emplace(id, std::move(record));
    return error::success;
  }

  [[nodiscard]] error register_type(const type_definition& desc,
                                    std::span<const property_binding> bindings = {}) {
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

    std::unique_lock lock(mutex_);
    if (is_frozen_) {
      return error::invalid_state;
    }
    if ((desc.guid && (find_type_unlocked(desc.guid) || find_enum_unlocked(desc.guid))) ||
        (desc.type_id && (by_cpp_type_.contains(desc.type_id) || enums_.contains(desc.type_id)))) {
      return error::invalid_argument;
    }

    auto record = std::make_unique<type_record>();
    record->guid = desc.guid;
    record->type_id = desc.type_id;
    record->native = desc.native;
    record->schema_version = desc.schema_version;
    record->name = string_name{desc.name};
    record->object = desc.object;
    record->extensions = desc.extensions;
    record->properties.reserve(desc.properties.size());
    for (const auto& property : desc.properties) {
      record->properties.push_back({.id = property.id.get(),
                                    .type_guid = property.type_guid,
                                    .access = property.access,
                                    .name = string_name{property.name},
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
  }

  [[nodiscard]] error freeze() {
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
      std::set<gneiss::type_id> chain;
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
          auto status = detail::visit_type_leaves(*access, [&](const type_ops& leaf) {
            if (leaf.kind() != type_kind::object)
              return error::success;
            const auto found = by_cpp_type_.find(leaf.id);
            if (found == by_cpp_type_.end())
              return leaf.representation ? error::success : error::not_found;
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
        if (found->second.scalar != property.accessor.kind)
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
        return properties[left].name.view() < properties[right].name.view();
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
  }

  [[nodiscard]] bool is_frozen() const {
    std::shared_lock lock(mutex_);
    return is_frozen_;
  }

  result<type_range> registered_types() const {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return error::not_ready;
    return type_range{owner(), types_.data(), types_.size()};
  }

  meta::type resolve_access(const type_ops& access) const {
    std::shared_lock lock(mutex_);
    const type_record* record = nullptr;
    if (is_frozen_) {
      if (access.enumeration) {
        const auto found = enums_.find(access.id);
        if (found != enums_.end())
          record = &found->second;
      } else {
        const auto found = by_cpp_type_.find(access.id);
        if (found != by_cpp_type_.end())
          record = found->second;
      }
    }
    return detail::type_factory::make(owner(), &access, record);
  }

  template <class Key> meta::type lookup_type(Key key) const {
    std::shared_lock lock(mutex_);
    if (!is_frozen_)
      return {};
    if (!key)
      return {};
    const type_record* record = nullptr;
    if constexpr (std::is_same_v<Key, gneiss::guid>) {
      record = find_type_unlocked(key);
      if (!record)
        record = find_enum_unlocked(key);
    } else {
      const auto found = by_cpp_type_.find(key);
      if (found != by_cpp_type_.end())
        record = found->second;
    }
    if (record)
      return detail::type_factory::make(owner(), nullptr, record);
    if constexpr (std::is_same_v<Key, gneiss::type_id>) {
      auto entry = enums_.find(key);
      if (entry != enums_.end())
        return detail::type_factory::make(owner(), nullptr, &entry->second);
    }
    const type_ops* found = nullptr;
    auto match = [&](const type_ops& access) {
      if constexpr (std::is_same_v<Key, gneiss::guid>)
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
      return detail::type_factory::make(owner(), found);
    for (const auto& registered_type : types_) {
      for (const auto& property : registered_type->properties) {
        const auto* access = property.accessor.access;
        for (std::size_t depth = 0; access && depth <= 64; ++depth) {
          if (match(*access)) {
            if (access->enumeration && !enums_.contains(access->id))
              return {};
            return detail::type_factory::make(owner(), access);
          }
          access = access->element_ops() ? &access->element() : nullptr;
        }
      }
    }
    return {};
  }

private:
  registry_ref owner() const {
    return std::const_pointer_cast<type_registry_state>(shared_from_this());
  }
  [[nodiscard]] const type_record* find_enum_unlocked(guid id) const noexcept {
    if (!id)
      return nullptr;
    for (const auto& [key, record] : enums_) {
      if (record.guid == id)
        return &record;
    }
    return nullptr;
  }
  [[nodiscard]] const type_record* find_type_unlocked(gneiss::type_id id) const noexcept {
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
  std::map<gneiss::type_id, type_record*> by_cpp_type_;
  std::map<gneiss::type_id, type_record> enums_;
  bool is_frozen_ = false;
};

registry_ref default_registry() {
  static const registry_ref registry = std::make_shared<type_registry_state>();
  return registry;
}

type_kind type::kind() const noexcept {
  return access_   ? access_->kind()
         : record_ ? (record_->enumeration ? type_kind::scalar : type_kind::object)
                   : type_kind::invalid;
}
bool type::is_enum() const noexcept {
  return access_ ? access_->enumeration : record_ && record_->enumeration;
}
gneiss::type_id type::id() const noexcept {
  return access_ ? access_->id : record_ ? record_->type_id : gneiss::type_id{};
}
gneiss::guid type::guid() const noexcept {
  return record_     ? record_->guid
         : is_enum() ? gneiss::guid{}
         : access_   ? access_->guid
                     : gneiss::guid{};
}
value_kind type::scalar_kind() const noexcept {
  return access_ ? access_->scalar_kind() : record_ ? record_->scalar : value_kind::invalid;
}
std::span<const enum_entry> cap::enumeration::entries() const noexcept {
  return type_.record_ ? std::span<const enum_entry>{type_.record_->entries}
                       : std::span<const enum_entry>{};
}
const property_binding* property::binding() const noexcept {
  return record_ ? &record_->accessor : nullptr;
}
property cap::object::find_property(property_id property) const noexcept {
  if (!*this)
    return {};
  if (type_.kind() != type_kind::object)
    return {};
  if (!type_.record_)
    return {};
  const auto found =
      std::ranges::lower_bound(type_.record_->properties, property.get(), {}, &property_record::id);
  if (found == type_.record_->properties.end() || found->id != property.get())
    return {};
  return meta::property{type_.registry_, &*found};
}
property cap::object::find_property(std::string_view name) const noexcept {
  if (!*this)
    return {};
  if (type_.kind() != type_kind::object)
    return {};
  if (!type_.record_)
    return {};
  const auto found = std::ranges::lower_bound(type_.record_->property_names, name, {},
                                              [&](std::size_t index) -> std::string_view {
                                                return type_.record_->properties[index].name.view();
                                              });
  if (found == type_.record_->property_names.end() ||
      type_.record_->properties[*found].name != name)
    return {};
  return meta::property{type_.registry_, &type_.record_->properties[*found]};
}
property_range cap::object::properties() const noexcept {
  if (type_.kind() != type_kind::object || !type_.record_)
    return {};
  return property_range{type_.registry_, type_.record_->properties.data(),
                        type_.record_->properties.size()};
}
bool operator==(const type& left, const type& right) noexcept {
  if (!left || !right)
    return bool(left) == bool(right);
  if (left.registry_ != right.registry_)
    return false;
  if (left.id() || right.id())
    return left.id() == right.id();
  return left.guid() == right.guid();
}
const void* type::extension(gneiss::type_id key) const noexcept {
  return record_ ? record_->extensions.find(key) : nullptr;
}
const void* property::extension(gneiss::type_id key) const noexcept {
  return record_ ? record_->extensions.find(key) : nullptr;
}
std::uint32_t type::schema_version() const noexcept {
  return record_ ? record_->schema_version : 0;
}
std::string_view type::name() const noexcept {
  return record_ ? record_->name.view() : std::string_view{};
}
meta::type resolve(const registry_ref& registry, gneiss::type_id id) {
  return registry ? registry->lookup_type(id) : meta::type{};
}
meta::type resolve(const registry_ref& registry, gneiss::guid id) {
  return registry ? registry->lookup_type(id) : meta::type{};
}
namespace detail {
meta::type resolve_type(const registry_ref& registry, const type_ops& access) {
  return registry ? registry->resolve_access(access) : type_factory::make({}, &access);
}
}
registry_ref create_registry() { return std::make_shared<type_registry_state>(); }
error register_enum(const registry_ref& state, const enum_definition& definition) {
  return state ? state->register_enum(definition) : error::invalid_handle;
}
error register_type(const registry_ref& state, const type_definition& desc) {
  return state ? state->register_type(desc) : error::invalid_handle;
}
error register_type(const registry_ref& state, const type_definition& desc,
                    std::span<const property_binding> bindings) {
  return state ? state->register_type(desc, bindings) : error::invalid_handle;
}

error freeze(const registry_ref& state) { return state ? state->freeze() : error::invalid_handle; }
bool is_frozen(const registry_ref& state) {
  GNEISS_ASSERT(state, "查询冻结状态要求有效注册表");
  return state->is_frozen();
}

property property_range::iterator::operator*() const noexcept {
  return property{registry_, records_ + index_};
}
property_id property::id() const noexcept {
  return record_ ? property_id{record_->id} : property_id{};
}
std::string_view property::name() const noexcept {
  return record_ ? record_->name.view() : std::string_view{};
}
gneiss::type_id property::declaring_type_id() const noexcept {
  return record_ ? record_->declaring_id : gneiss::type_id{};
}
gneiss::guid property::declaring_type() const noexcept {
  return record_ ? record_->declaring_type : gneiss::guid{};
}
meta::type property::type() const {
  return record_ ? record_type(registry_, *record_) : meta::type{};
}

property_capabilities property::capabilities() const noexcept {
  return record_ ? record_capabilities(*record_) : property_capabilities::none;
}
}

namespace gneiss::meta {
property property_range::operator[](std::size_t index) const noexcept {
  return property{registry_, records_ + index};
}
}

namespace gneiss::meta {
meta::type cap::object::base_type() const {
  return type_.record_ && type_.record_->object.base_type
             ? resolve(type_.registry_, type_.record_->object.base_type)
             : meta::type{};
}
bool cap::object::can_construct() const noexcept {
  return type_.record_ && type_.record_->object.construct;
}
result<ref<void>> cap::object::construct() const {
  if (!can_construct())
    return error::unsupported;
  auto owner = type_.record_->object.construct();
  GNEISS_ASSERT(owner, "对象构造器必须返回存储，分配失败通过异常传播");
  return owner;
}
result<const void*> cap::object::cast_address(meta::type target, const void* address) const {
  if (!type_ || !target || type_.registry_ != target.registry_)
    return error::invalid_argument;
  auto current = type_;
  for (std::size_t depth = 0; depth <= 64; ++depth) {
    if (current == target)
      return address;
    if (!current.record_)
      return error::unsupported;
    const auto& ops = current.record_->object;
    if (target.id() == runtime_type_of<meta::object>()) {
      if (!ops.as_object)
        return error::unsupported;
      return address ? static_cast<const void*>(ops.as_object(const_cast<void*>(address)))
                     : nullptr;
    }
    if (!ops.base_type || !ops.upcast)
      return error::invalid_argument;
    if (address)
      address = ops.upcast(const_cast<void*>(address));
    current = resolve(type_.registry_, ops.base_type);
    if (!current)
      return error::not_found;
  }
  return error::invalid_state;
}
bool cap::object::is_a(meta::type target) const {
  return bool(cast_address(std::move(target), nullptr));
}
}

namespace gneiss::meta {
result<type_range> registered_types(const registry_ref& registry) {
  return registry ? registry->registered_types() : result<type_range>{error::invalid_handle};
}
meta::type type_range::operator[](std::size_t index) const noexcept {
  GNEISS_ASSERT(index < size_, "类型范围下标必须有效");
  return detail::type_factory::make(registry_, nullptr, records_[index].get());
}
meta::type type_range::iterator::operator*() const noexcept {
  return detail::type_factory::make(registry_, nullptr, records_[index_].get());
}
}
