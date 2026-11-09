// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/detail/type_ops.hpp"
#include "engine/core/meta/object.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/traits/type_identity.hpp"
#include <functional>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace gneiss::engine::meta {
/** 注册 Meta 基础值及数学类型；不冻结注册表，重复注册按普通类型注册规则报错。 */
GNEISS_API error register_builtin_types(const registry_ref&) noexcept;
inline error register_builtin_types() noexcept {
  auto registry = default_registry();
  return registry ? register_builtin_types(*registry) : registry.error();
}

/** 从规范化 C++ 类型提取身份；显式身份与 traits 冲突时拒绝注册。 */
template <class T>
[[nodiscard]] error register_type(const registry_ref& registry, type_definition definition,
                                  std::span<const property_binding> bindings = {}) noexcept {
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
  return register_type(registry, definition, bindings);
}
template <class T>
[[nodiscard]] error register_type(type_definition definition,
                                  std::span<const property_binding> bindings = {}) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return register_type<T>(*registry, definition, bindings);
}
/** C++ 枚举的类型安全注册；只保存显示项，不自动约束枚举业务值域。 */
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
  enum_builder& value(std::string_view name, E input) noexcept {
    if (committed_ || status_.failed())
      return *this;
    try {
      entries_.push_back({std::string(name), detail::scalar_value(input)});
    } catch (const std::bad_alloc&) {
      status_ = error::out_of_memory;
    } catch (...) {
      status_ = error::internal;
    }
    return *this;
  }
  error commit() noexcept {
    if (committed_)
      return error::invalid_state;
    committed_ = true;
    if (status_.failed())
      return status_;
    return register_enum(registry_, runtime_type_of<E>(), detail::reflected_value_kind<E>(),
                         entries_);
  }

private:
  registry_ref registry_;
  std::vector<enum_entry> entries_;
  error status_ = error::success;
  bool committed_ = false;
};
/** 注册期间拥有字段名称；不持有实例。需外部同步，commit 后不可复用。
 * 访问器只在调用期间借用对象，setter 须先校验再修改，失败保持原对象。
 * 分配错误记录为结果码，commit 失败不在 Registry 留下类型。 */
template <class T> class registration {
  using object_type = std::remove_cvref_t<T>;
  struct entry {
    property_id id;
    std::string name;
    engine::guid guid;
    property_flags flags;
    property_binding binding;
    meta::metadata extensions{};
  };

public:
  registration() noexcept {
    auto registry = default_registry();
    if (registry)
      registry_ = std::move(*registry);
    else
      status_ = registry.error();
  }
  explicit registration(registry_ref registry) noexcept : registry_(std::move(registry)) {}
  /** 显式补充身份；与已有 traits 冲突时由 commit 拒绝。名称在注册期间自有。 */
  registration& name(std::string_view name) noexcept {
    if (committed_ || status_.failed())
      return *this;
    try {
      name_ = name;
    } catch (const std::bad_alloc&) {
      status_ = error::out_of_memory;
    } catch (...) {
      status_ = error::internal;
    }
    return *this;
  }
  registration& guid(engine::guid guid) noexcept {
    if (!committed_ && !status_.failed())
      guid_ = guid;
    return *this;
  }
  /** 类型扩展由消费模块解释，注册器仅按类型保存不可变副本。 */
  template <class Extension> registration& metadata(Extension value) noexcept {
    if (!committed_ && status_.ok())
      status_ = extensions_.insert(std::move(value));
    return *this;
  }
  /** 为指定已登记字段附加扩展；字段不存在或同类扩展重复均失败。 */
  template <class Extension>
  registration& property_metadata(property_id id, Extension value) noexcept {
    if (committed_ || status_.failed())
      return *this;
    for (auto& entry : entries_) {
      if (entry.id == id) {
        status_ = entry.extensions.insert(std::move(value));
        return *this;
      }
    }
    status_ = error::not_found;
    return *this;
  }
  registration& schema_version(std::uint32_t version) noexcept {
    if (committed_ || status_.failed())
      return *this;
    version_ = version;
    return *this;
  }
  template <auto Member>
  registration& property(property_id id, std::string_view name,
                         property_flags flags = property_flags::none) noexcept {
    static_assert(std::is_member_object_pointer_v<decltype(Member)>);
    using property_type = std::remove_cvref_t<std::invoke_result_t<decltype(Member), object_type&>>;
    if constexpr (detail::reflected_value_kind<property_type>() == value_kind::invalid) {
      static_assert(std::is_class_v<property_type> && std::is_move_assignable_v<property_type>,
                    "Nested properties must be assignable value objects");
      return add<property_type>(
          id, name, flags,
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
      return add<property_type>(id, name, flags,
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
  registration& property(property_id id, std::string_view name,
                         property_flags flags = property_flags::none) noexcept {
    using property_type =
        std::remove_cvref_t<std::invoke_result_t<decltype(Getter), const object_type&>>;
    if constexpr (detail::reflected_value_kind<property_type>() == value_kind::invalid) {
      using returned = std::invoke_result_t<decltype(Getter), const object_type&>;
      static_assert(std::is_lvalue_reference_v<returned> &&
                        std::is_const_v<std::remove_reference_t<returned>>,
                    "Object getters must borrow a const lvalue, not a temporary");
      static_assert(std::is_default_constructible_v<property_type>);
      return add<property_type>(
          id, name, flags,
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
          id, name, flags,
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
  template <auto Getter> registration& readonly(property_id id, std::string_view name) noexcept {
    using property_type =
        std::remove_cvref_t<std::invoke_result_t<decltype(Getter), const object_type&>>;
    return add<property_type>(id, name, property_flags::read_only,
                              {.kind = detail::reflected_value_kind<property_type>(),
                               .getter = [](void*, const void* object, value& output) -> error {
                                 output = detail::scalar_value(
                                     std::invoke(Getter, *static_cast<const object_type*>(object)));
                                 return error::success;
                               }});
  }
  /** 以本类型声明的引用始终保存实际类型信封；不自动启用派生类的多态声明。 */
  registration& polymorphic() noexcept {
    static_assert(std::derived_from<object_type, meta::object>);
    if (committed_) {
      status_ = error::invalid_state;
      return *this;
    }
    codec_.polymorphic = true;
    return *this;
  }
  /** 注册唯一直接基类；继承属性参与遍历，冻结时拒绝属性 ID 或名称冲突。 */
  template <class Base> registration& base() noexcept {
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
  /** 启用格式无关字段遍历；缺失的非必填字段保留默认值，只读字段不持久化。 */
  registration& serialization(bool reject_unknown_fields = false) noexcept {
    static_assert(std::is_default_constructible_v<object_type>);
    if (committed_ || status_.failed())
      return *this;
    if (codec_.reflected) {
      status_ = error::invalid_argument;
      return *this;
    }
    codec_.reflected = true;
    codec_.reject_unknown_fields = reject_unknown_fields;
    object_.construct = []() -> ref<void> { return make_ref<object_type>(); };
    return *this;
  }
  /** 校验返回 error 或 serialization::diagnostic；不修改对象。 */
  template <auto Validate> registration& validate() noexcept {
    if (committed_ || status_.failed())
      return *this;
    if (codec_.validate) {
      status_ = error::invalid_argument;
      return *this;
    }
    codec_.validate = [](const void* object) -> serialization::diagnostic {
      auto status = std::invoke(Validate, *static_cast<const object_type*>(object));
      if constexpr (std::is_same_v<decltype(status), error>)
        return {.error = status};
      else
        return status;
    };
    return *this;
  }
  /** 通用有类型 codec；借用上下文与后端，允许 nullptr 表示缺少某个方向。 */
  template <auto Encode, auto Decode> registration& serialization() noexcept {
    if (committed_ || status_.failed())
      return *this;
    if (has_serialization_) {
      status_ = error::invalid_argument;
      return *this;
    }
    has_serialization_ = true;
    if constexpr (!std::is_null_pointer_v<decltype(Encode)>) {
      auto encode = Encode;
      if (encode)
        codec_.encode = [](const serialization::context& ctx, serialization::writer& output,
                           const void* object) -> result<void*, serialization::diagnostic> {
          return std::invoke(Encode, ctx, output, *static_cast<const object_type*>(object));
        };
    }
    if constexpr (!std::is_null_pointer_v<decltype(Decode)>) {
      auto decode = Decode;
      if (decode)
        codec_.decode = [](const serialization::context& ctx, serialization::reader& input,
                           const void* node) -> result<ref<void>, serialization::diagnostic> {
          auto decoded = std::invoke(Decode, ctx, input, node);
          if (!decoded)
            return std::move(decoded).error();
          return ref<void>{std::make_shared<object_type>(std::move(*decoded))};
        };
    }
    if (!codec_.encode && !codec_.decode)
      status_ = error::invalid_argument;
    return *this;
  }
  [[nodiscard]] error commit() noexcept {
    if (committed_)
      return error::invalid_state;
    committed_ = true;
    if (status_.failed())
      return status_;
    try {
      std::vector<property_definition> properties;
      std::vector<property_binding> bindings;
      properties.reserve(entries_.size());
      bindings.reserve(entries_.size());
      for (const auto& item : entries_) {
        properties.push_back({item.id, item.guid, item.flags, item.name, item.extensions});
        bindings.push_back(item.binding);
      }
      if constexpr (std::derived_from<object_type, meta::object>)
        object_.as_object = [](void* p) -> meta::object* { return static_cast<object_type*>(p); };
      return register_type<object_type>(registry_,
                                        {.guid = guid_,
                                         .schema_version = version_,
                                         .name = name_,
                                         .properties = properties,
                                         .object = object_,
                                         .serialization = codec_,
                                         .extensions = extensions_},
                                        bindings);
    } catch (const std::bad_alloc&) {
      return error::out_of_memory;
    } catch (...) {
      return error::internal;
    }
  }

private:
  template <class Property>
  registration& add(property_id id, std::string_view name, property_flags flags,
                    property_binding binding) noexcept {
    if (committed_ || status_.failed())
      return *this;
    try {
      binding.access = &detail::ops_of<Property>();
      if constexpr (std::is_enum_v<Property>)
        binding.enum_type = runtime_type_of<Property>();
      if (has_flags(flags, property_flags::read_only)) {
        binding.setter = nullptr;
        binding.object_mutator = nullptr;
        binding.object_assign = nullptr;
      }
      entries_.push_back({id, std::string(name),
                          type_identity<detail::scalar_storage<Property>>::guid(), flags, binding});
    } catch (const std::bad_alloc&) {
      status_ = error::out_of_memory;
    } catch (...) {
      status_ = error::internal;
    }
    return *this;
  }
  object_ops object_{};
  serialization::binding codec_{};
  meta::metadata extensions_{};
  bool has_serialization_ = false;
  registry_ref registry_;
  std::vector<entry> entries_;
  std::string name_;
  engine::guid guid_{};
  std::uint32_t version_ = 1;
  error status_ = error::success;
  bool committed_ = false;
};
}
