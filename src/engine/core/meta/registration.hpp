// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/detail/type_access.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/traits/type_identity.hpp"
#include <functional>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace gneiss::engine::meta {
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
template <class T> class type_builder {
  using object_type = std::remove_cvref_t<T>;
  struct entry {
    property_id id;
    std::string name;
    engine::guid guid;
    property_flags flags;
    property_binding binding;
  };

public:
  type_builder() noexcept {
    auto registry = default_registry();
    if (registry)
      registry_ = std::move(*registry);
    else
      status_ = registry.error();
  }
  explicit type_builder(registry_ref registry) noexcept : registry_(std::move(registry)) {}
  type_builder& schema_version(std::uint32_t version) noexcept {
    version_ = version;
    return *this;
  }
  template <auto Member>
  type_builder& property(property_id id, std::string_view name,
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
           .access = &detail::access_of<property_type>()});
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
  type_builder& property(property_id id, std::string_view name,
                         property_flags flags = property_flags::none) noexcept {
    using property_type =
        std::remove_cvref_t<std::invoke_result_t<decltype(Getter), const object_type&>>;
    return add<property_type>(
        id, name, flags,
        {.kind = detail::reflected_value_kind<property_type>(),
         .getter = [](void*, const void* object, value& output) -> error {
           output =
               detail::scalar_value(std::invoke(Getter, *static_cast<const object_type*>(object)));
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
  template <auto Getter> type_builder& readonly(property_id id, std::string_view name) noexcept {
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
  /** 启用反射 JSON；缺失的非必填字段保留默认值，只读字段不持久化。 */
  type_builder& serialization(bool reject_unknown_fields = false) noexcept {
    static_assert(std::is_default_constructible_v<object_type>);
    if (committed_ || status_.failed())
      return *this;
    if (codec_.reflected_json) {
      status_ = error::invalid_argument;
      return *this;
    }
    codec_.reflected_json = true;
    codec_.reject_unknown_fields = reject_unknown_fields;
    codec_.construct = []() -> ref<void> { return make_ref<object_type>(); };
    return *this;
  }
  /** 校验返回 error 或 serialization_diagnostic；不修改对象。 */
  template <auto Validate> type_builder& validate() noexcept {
    if (committed_ || status_.failed())
      return *this;
    if (codec_.validate) {
      status_ = error::invalid_argument;
      return *this;
    }
    codec_.validate = [](const void* object) -> serialization_diagnostic {
      auto status = std::invoke(Validate, *static_cast<const object_type*>(object));
      if constexpr (std::is_same_v<decltype(status), error>)
        return {.error = status};
      else
        return status;
    };
    return *this;
  }
  /** Encode/Decode 是有类型函数，允许 nullptr 表示缺少该方向。文件格式由 codec 自行校验。 */
  template <auto Encode, auto Decode> type_builder& serialization() noexcept {
    if (committed_ || status_.failed())
      return *this;
    if (has_serialization_) {
      status_ = error::invalid_argument;
      return *this;
    }
    has_serialization_ = true;
    if constexpr (Encode == nullptr && Decode == nullptr) {
      status_ = error::invalid_argument;
      return *this;
    }
    if constexpr (Encode != nullptr)
      codec_.encode =
          [](const registry_ref& registry,
             const void* object) -> result<std::vector<std::byte>, serialization_diagnostic> {
        if constexpr (std::is_invocable_v<decltype(Encode), const registry_ref&,
                                          const object_type&>)
          return std::invoke(Encode, registry, *static_cast<const object_type*>(object));
        else
          return std::invoke(Encode, *static_cast<const object_type*>(object));
      };
    if constexpr (Decode != nullptr)
      codec_.decode =
          [](const registry_ref& registry,
             std::span<const std::byte> bytes) -> result<ref<void>, serialization_diagnostic> {
        auto decoded = [&] {
          if constexpr (std::is_invocable_v<decltype(Decode), const registry_ref&,
                                            std::span<const std::byte>>)
            return std::invoke(Decode, registry, bytes);
          else
            return std::invoke(Decode, bytes);
        }();
        if (!decoded)
          return std::move(decoded).error();
        return ref<void>{std::make_shared<object_type>(std::move(*decoded))};
      };
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
        properties.push_back({item.id, item.guid, item.flags, item.name});
        bindings.push_back(item.binding);
      }
      return register_type<object_type>(
          registry_,
          {.schema_version = version_, .properties = properties, .serialization = codec_},
          bindings);
    } catch (const std::bad_alloc&) {
      return error::out_of_memory;
    } catch (...) {
      return error::internal;
    }
  }

private:
  template <class Property>
  type_builder& add(property_id id, std::string_view name, property_flags flags,
                    property_binding binding) noexcept {
    if (committed_ || status_.failed())
      return *this;
    try {
      binding.access = &detail::access_of<Property>();
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
  serialization_binding codec_{};
  bool has_serialization_ = false;
  registry_ref registry_;
  std::vector<entry> entries_;
  std::uint32_t version_ = 1;
  error status_ = error::success;
  bool committed_ = false;
};
}
