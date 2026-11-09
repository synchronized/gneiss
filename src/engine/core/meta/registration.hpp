// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/detail/type_ops.hpp"
#include "engine/core/meta/object_type.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/traits/type_identity.hpp"
#include <functional>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace gneiss::engine::meta {
namespace detail {
template <class T, class Definition> error complete_identity(Definition& definition) noexcept {
  using type = std::remove_cvref_t<T>;
  const auto guid = type_identity<type>::guid();
  const auto name = type_identity<type>::name();
  if ((guid && definition.guid && guid != definition.guid) ||
      (!name.empty() && !definition.name.empty() && name != definition.name))
    return error::invalid_argument;
  if (!definition.guid)
    definition.guid = guid;
  if (definition.name.empty())
    definition.name = name;
  const auto id = runtime_type_of<type>();
  if (!id || (definition.type_id && definition.type_id != id))
    return error::invalid_argument;
  definition.type_id = id;
  definition.native = &detail::ops_of<type>();
  return error::success;
}
}
/** 从规范化 C++ 类型提取身份；显式身份与 traits 冲突时拒绝注册。 */
template <class T>
[[nodiscard]] error register_type(const registry_ref& registry, type_definition definition,
                                  std::span<const property_binding> bindings = {}) {
  auto status = detail::complete_identity<T>(definition);
  return status.failed() ? status : register_type(registry, definition, bindings);
}

template <class T>
[[nodiscard]] error register_type(type_definition definition,
                                  std::span<const property_binding> bindings = {}) {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return register_type<T>(*registry, definition, bindings);
}
namespace detail {
/** C++ 枚举的类型安全注册；保存类型身份、扩展及显示项，不自动约束枚举业务值域。 */
template <class E>
  requires std::is_enum_v<E>
class enum_builder {
public:
  enum_builder() noexcept {
    auto registry = default_registry();
    if (registry)
      registry_ = std::move(*registry);
    else
      status_ = registry.error();
  }
  explicit enum_builder(registry_ref registry) noexcept : registry_(std::move(registry)) {}
  /** 显式补充身份；与已有 traits 冲突时由 commit 拒绝。名称在注册期间自有。 */
  enum_builder& name(std::string_view name) {
    if (committed_ || status_.failed())
      return *this;
    name_ = name;
    return *this;
  }
  enum_builder& guid(engine::guid guid) noexcept {
    if (!committed_ && !status_.failed())
      guid_ = guid;
    return *this;
  }
  /** 类型扩展由消费模块解释，注册器仅按类型保存不可变副本。 */
  template <class Extension> enum_builder& metadata(Extension value) {
    if (!committed_ && status_.ok())
      status_ = extensions_.insert(std::move(value));
    return *this;
  }
  enum_builder& schema_version(std::uint32_t version) noexcept {
    if (committed_ || status_.failed())
      return *this;
    version_ = version;
    return *this;
  }
  /** 按注册对象类型生成扩展；生成错误记录到 commit，不引入消费模块依赖。 */
  template <class Factory> enum_builder& extend(Factory factory) {
    if (committed_ || status_.failed())
      return *this;
    auto extension = factory.template operator()<E>();
    if (!extension)
      status_ = extension.error();
    else
      status_ = extensions_.insert(std::move(*extension));
    return *this;
  }
  enum_builder& value(std::string_view name, E input) {
    if (committed_ || status_.failed())
      return *this;
    entries_.push_back({std::string(name), detail::scalar_value(input)});
    return *this;
  }
  error commit() {
    if (committed_)
      return error::invalid_state;
    committed_ = true;
    if (status_.failed())
      return status_;
    enum_definition definition{.guid = guid_,
                               .schema_version = version_,
                               .name = name_,
                               .kind = detail::reflected_value_kind<E>(),
                               .entries = entries_,
                               .extensions = extensions_};
    auto status = detail::complete_identity<E>(definition);
    return status.failed() ? status : register_enum(registry_, definition);
  }

private:
  registry_ref registry_;
  std::vector<enum_entry> entries_;
  std::string name_;
  engine::guid guid_{};
  std::uint32_t version_ = 1;
  meta::metadata extensions_{};
  error status_ = error::success;
  bool committed_ = false;
};
/** 注册期间拥有字段名称；不持有实例。需外部同步，commit 后不可复用。
 * 访问器只在调用期间借用对象，setter 须先校验再修改，失败保持原对象。
 * 预期错误返回结果码，分配或用户扩展异常向调用方传播。 */
template <class T> class type_builder {
  using object_type = std::remove_cvref_t<T>;
  struct entry {
    property_id id;
    std::string name;
    engine::guid guid;
    property_access access;
    property_binding binding;
    meta::metadata extensions{};
  };

public:
  /** 借用注册器及稳定字段索引；注册器移动/销毁后失效，不隐式提交。
   * operator() 仅附加当前字段扩展，metadata/extend 仍作用于类型。 */
  class property_builder {
  public:
    template <class... Extensions> property_builder& operator()(Extensions... values) {
      (owner_->add_extension(index_, std::move(values)), ...);
      return *this;
    }
    template <auto... Accessors>
    property_builder property(property_id id, std::string_view name,
                              property_access access = property_access::read_write) {
      return owner_->template property<Accessors...>(id, name, access);
    }
    template <auto Getter> property_builder readonly(property_id id, std::string_view name) {
      return owner_->template readonly<Getter>(id, name);
    }
    template <class Base> type_builder& base() { return owner_->template base<Base>(); }
    template <class Extension> type_builder& metadata(Extension value) {
      return owner_->metadata(std::move(value));
    }
    template <class Factory> type_builder& extend(Factory factory) {
      return owner_->extend(std::move(factory));
    }
    type_builder& name(std::string_view name) { return owner_->name(name); }
    type_builder& guid(engine::guid guid) noexcept { return owner_->guid(guid); }
    type_builder& schema_version(std::uint32_t version) noexcept {
      return owner_->schema_version(version);
    }
    [[nodiscard]] error commit() { return owner_->commit(); }

  private:
    friend class type_builder;
    property_builder(type_builder& owner, std::size_t index) : owner_(&owner), index_(index) {}
    type_builder* owner_;
    std::size_t index_;
  };
  type_builder() noexcept {
    auto registry = default_registry();
    if (registry)
      registry_ = std::move(*registry);
    else
      status_ = registry.error();
  }
  explicit type_builder(registry_ref registry) noexcept : registry_(std::move(registry)) {}
  /** 显式补充身份；与已有 traits 冲突时由 commit 拒绝。名称在注册期间自有。 */
  type_builder& name(std::string_view name) {
    if (committed_ || status_.failed())
      return *this;
    name_ = name;
    return *this;
  }
  type_builder& guid(engine::guid guid) noexcept {
    if (!committed_ && !status_.failed())
      guid_ = guid;
    return *this;
  }
  /** 类型扩展由消费模块解释，注册器仅按类型保存不可变副本。 */
  template <class Extension> type_builder& metadata(Extension value) {
    if (!committed_ && status_.ok())
      status_ = extensions_.insert(std::move(value));
    return *this;
  }
  type_builder& schema_version(std::uint32_t version) noexcept {
    if (committed_ || status_.failed())
      return *this;
    version_ = version;
    return *this;
  }
  template <auto Member>
  property_builder property(property_id id, std::string_view name,
                            property_access access = property_access::read_write) {
    static_assert(std::is_member_object_pointer_v<decltype(Member)>);
    using property_type = std::remove_cvref_t<std::invoke_result_t<decltype(Member), object_type&>>;
    if constexpr (detail::reflected_value_kind<property_type>() == value_kind::invalid) {
      static_assert(std::is_class_v<property_type> && std::is_move_assignable_v<property_type>,
                    "Nested properties must be assignable value objects");
      return add<property_type>(
          id, name, access,
          {.object_type = runtime_type_of<property_type>(),
           .object_getter = [](const void* object) -> const void* {
             return std::addressof(std::invoke(Member, *static_cast<const object_type*>(object)));
           },
           .object_mutator = [](void* object) -> void* {
             return std::addressof(std::invoke(Member, *static_cast<object_type*>(object)));
           },
           .object_assign =
               [](void* object, void* value) {
                 std::invoke(Member, *static_cast<object_type*>(object)) =
                     std::move(*static_cast<property_type*>(value));
               },
           .access = &detail::ops_of<property_type>()});
    } else
      return add<property_type>(id, name, access,
                                {.kind = detail::reflected_value_kind<property_type>(),
                                 .getter = [](void*, const void* object, value& output) -> error {
                                   output = detail::scalar_value(std::invoke(
                                       Member, *static_cast<const object_type*>(object)));
                                   return error::success;
                                 },
                                 .setter = [](void*, void* object, const value& input) -> error {
                                   auto decoded = detail::scalar_cast<property_type>(input);
                                   if (!decoded)
                                     return decoded.error();
                                   std::invoke(Member, *static_cast<object_type*>(object)) =
                                       std::move(*decoded);
                                   return error::success;
                                 }});
  }
  template <auto Getter, auto Setter>
  property_builder property(property_id id, std::string_view name,
                            property_access access = property_access::read_write) {
    using property_type =
        std::remove_cvref_t<std::invoke_result_t<decltype(Getter), const object_type&>>;
    if constexpr (detail::reflected_value_kind<property_type>() == value_kind::invalid) {
      using returned = std::invoke_result_t<decltype(Getter), const object_type&>;
      static_assert(std::is_lvalue_reference_v<returned> &&
                        std::is_const_v<std::remove_reference_t<returned>>,
                    "Object getters must borrow a const lvalue, not a temporary");
      static_assert(std::is_default_constructible_v<property_type>);
      return add<property_type>(
          id, name, access,
          {.object_type = runtime_type_of<property_type>(),
           .object_getter = [](const void* object) -> const void* {
             return std::addressof(std::invoke(Getter, *static_cast<const object_type*>(object)));
           },
           .object_setter = [](void* object, const void* value) -> error {
             if constexpr (std::is_void_v<std::invoke_result_t<decltype(Setter), object_type&,
                                                               const property_type&>>) {
               std::invoke(Setter, *static_cast<object_type*>(object),
                           *static_cast<const property_type*>(value));
               return error::success;
             } else {
               return std::invoke(Setter, *static_cast<object_type*>(object),
                                  *static_cast<const property_type*>(value));
             }
           },
           .object_construct = []() -> ref<void> { return make_ref<property_type>(); },
           .access = &detail::ops_of<property_type>()});
    } else
      return add<property_type>(
          id, name, access,
          {.kind = detail::reflected_value_kind<property_type>(),
           .getter = [](void*, const void* object, value& output) -> error {
             output = detail::scalar_value(
                 std::invoke(Getter, *static_cast<const object_type*>(object)));
             return error::success;
           },
           .setter = [](void*, void* object, const value& input) -> error {
             auto decoded = detail::scalar_cast<property_type>(input);
             if (!decoded)
               return decoded.error();
             if constexpr (std::is_void_v<std::invoke_result_t<decltype(Setter), object_type&,
                                                               const property_type&>>) {
               std::invoke(Setter, *static_cast<object_type*>(object), *decoded);
               return error::success;
             } else {
               return std::invoke(Setter, *static_cast<object_type*>(object), *decoded);
             }
           }});
  }
  template <auto Getter> property_builder readonly(property_id id, std::string_view name) {
    using property_type =
        std::remove_cvref_t<std::invoke_result_t<decltype(Getter), const object_type&>>;
    return add<property_type>(id, name, property_access::read_only,
                              {.kind = detail::reflected_value_kind<property_type>(),
                               .getter = [](void*, const void* object, value& output) -> error {
                                 output = detail::scalar_value(
                                     std::invoke(Getter, *static_cast<const object_type*>(object)));
                                 return error::success;
                               }});
  }
  /** 注册唯一直接基类；继承属性参与遍历，冻结时拒绝属性 ID 或名称冲突。 */
  template <class Base> type_builder& base() {
    static_assert(std::derived_from<object_type, Base> && std::derived_from<Base, meta::object>);
    if (committed_ || object_.base_type) {
      status_ = error::invalid_state;
      return *this;
    }
    object_.base_type = runtime_type_of<Base>();
    object_.upcast = [](void* p) -> void* {
      return static_cast<Base*>(static_cast<object_type*>(p));
    };
    return *this;
  }
  /** 按注册对象类型生成扩展；生成错误记录到 commit，不引入消费模块依赖。 */
  template <class Factory> type_builder& extend(Factory factory) {
    if (committed_ || status_.failed())
      return *this;
    auto extension = factory.template operator()<object_type>();
    if (!extension)
      status_ = extension.error();
    else
      status_ = extensions_.insert(std::move(*extension));
    return *this;
  }
  [[nodiscard]] error commit() {
    if (committed_)
      return error::invalid_state;
    committed_ = true;
    if (status_.failed())
      return status_;
    std::vector<property_definition> properties;
    std::vector<property_binding> bindings;
    properties.reserve(entries_.size());
    bindings.reserve(entries_.size());
    for (const auto& item : entries_) {
      properties.push_back({item.id, item.guid, item.access, item.name, item.extensions});
      bindings.push_back(item.binding);
    }
    if constexpr (std::derived_from<object_type, meta::object>)
      object_.as_object = [](void* p) -> meta::object* { return static_cast<object_type*>(p); };
    if constexpr (std::is_default_constructible_v<object_type>)
      object_.construct = []() -> ref<void> { return make_ref<object_type>(); };
    return register_type<object_type>(registry_,
                                      {.guid = guid_,
                                       .schema_version = version_,
                                       .name = name_,
                                       .properties = properties,
                                       .object = object_,
                                       .extensions = extensions_},
                                      bindings);
  }

private:
  template <class Extension> void add_extension(std::size_t index, Extension value) {
    if (committed_ || status_.failed())
      return;
    if (index >= entries_.size()) {
      status_ = error::invalid_state;
      return;
    }
    status_ = entries_[index].extensions.insert(std::move(value));
  }
  template <class Property>
  property_builder add(property_id id, std::string_view name, property_access access,
                       property_binding binding) {
    const auto index = entries_.size();
    if (committed_ || status_.failed())
      return {*this, index};

    binding.access = &detail::ops_of<Property>();
    if constexpr (std::is_enum_v<Property>)
      binding.enum_type = runtime_type_of<Property>();
    if (access == property_access::read_only) {
      binding.setter = nullptr;
      binding.object_mutator = nullptr;
      binding.object_assign = nullptr;
    }
    entries_.push_back({id, std::string(name),
                        type_identity<detail::scalar_storage<Property>>::guid(), access, binding});

    return {*this, index};
  }
  object_ops object_{};
  meta::metadata extensions_{};
  registry_ref registry_;
  std::vector<entry> entries_;
  std::string name_;
  engine::guid guid_{};
  std::uint32_t version_ = 1;
  error status_ = error::success;
  bool committed_ = false;
};
} // namespace detail
namespace registration {
/** 注册非枚举类型；借用显式注册表，省略时使用默认表。 */
template <class T>
  requires(!std::is_enum_v<std::remove_cvref_t<T>>)
auto type() noexcept {
  return detail::type_builder<T>{};
}
template <class T>
  requires(!std::is_enum_v<std::remove_cvref_t<T>>)
auto type(registry_ref registry) noexcept {
  return detail::type_builder<T>{std::move(registry)};
}
/** 注册枚举显示项及类型元数据，不改变枚举的整数表示。 */
template <class E>
  requires std::is_enum_v<std::remove_cvref_t<E>>
auto enumeration() noexcept {
  return detail::enum_builder<std::remove_cvref_t<E>>{};
}
template <class E>
  requires std::is_enum_v<std::remove_cvref_t<E>>
auto enumeration(registry_ref registry) noexcept {
  return detail::enum_builder<std::remove_cvref_t<E>>{std::move(registry)};
}
}
}
