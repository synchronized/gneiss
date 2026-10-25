// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/reflection/type_registry.hpp"
#include <functional>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace gneiss::engine {
namespace detail {
template <class T, std::size_t Index = 1> constexpr value_kind reflected_value_kind() {
  if constexpr (Index == std::variant_size_v<value::storage_type>) {
    static_assert(Index != std::variant_size_v<value::storage_type>,
                  "Unsupported property value type");
    return value_kind::invalid;
  } else if constexpr (std::is_same_v<T, std::variant_alternative_t<Index, value::storage_type>>) {
    return static_cast<value_kind>(Index);
  } else {
    return reflected_value_kind<T, Index + 1>();
  }
}
}
/** 注册期间拥有字段名称；不持有实例。需外部同步，commit 后不可复用。
 * 访问器只在调用期间借用对象，setter 须先校验再修改，失败保持原对象。
 * 分配错误记录为结果码，commit 失败不在 Registry 留下类型。 */
template <class T> class type_builder {
  using object_type = std::remove_cvref_t<T>;
  struct entry {
    field_id id;
    std::string name;
    engine::type_guid guid;
    field_flags flags;
    property_binding binding;
  };

public:
  explicit type_builder(registry_ref registry) noexcept : registry_(std::move(registry)) {}
  type_builder& schema_version(std::uint32_t version) noexcept {
    version_ = version;
    return *this;
  }
  template <auto Member> type_builder& property(field_id id, std::string_view name) noexcept {
    static_assert(std::is_member_object_pointer_v<decltype(Member)>);
    using property_type = std::remove_cvref_t<std::invoke_result_t<decltype(Member), object_type&>>;
    return add<property_type>(id, name, field_flags::none,
                              {.kind = detail::reflected_value_kind<property_type>(),
                               .getter = [](void*, const void* object, value& output) -> result {
                                 output.payload =
                                     std::invoke(Member, *static_cast<const object_type*>(object));
                                 return result::success;
                               },
                               .setter = [](void*, void* object, const value& input) -> result {
                                 std::invoke(Member, *static_cast<object_type*>(object)) =
                                     std::get<property_type>(input.payload);
                                 return result::success;
                               }});
  }
  template <auto Getter, auto Setter>
  type_builder& property(field_id id, std::string_view name) noexcept {
    using property_type =
        std::remove_cvref_t<std::invoke_result_t<decltype(Getter), const object_type&>>;
    return add<property_type>(
        id, name, field_flags::none,
        {.kind = detail::reflected_value_kind<property_type>(),
         .getter = [](void*, const void* object, value& output) -> result {
           output.payload = std::invoke(Getter, *static_cast<const object_type*>(object));
           return result::success;
         },
         .setter = [](void*, void* object, const value& input) -> result {
           if constexpr (std::is_void_v<std::invoke_result_t<decltype(Setter), object_type&,
                                                             const property_type&>>) {
             std::invoke(Setter, *static_cast<object_type*>(object),
                         std::get<property_type>(input.payload));
             return result::success;
           } else {
             return std::invoke(Setter, *static_cast<object_type*>(object),
                                std::get<property_type>(input.payload));
           }
         }});
  }
  template <auto Getter> type_builder& readonly(field_id id, std::string_view name) noexcept {
    using property_type =
        std::remove_cvref_t<std::invoke_result_t<decltype(Getter), const object_type&>>;
    return add<property_type>(id, name, field_flags::read_only,
                              {.kind = detail::reflected_value_kind<property_type>(),
                               .getter = [](void*, const void* object, value& output) -> result {
                                 output.payload =
                                     std::invoke(Getter, *static_cast<const object_type*>(object));
                                 return result::success;
                               }});
  }
  /** Encode/Decode 是有类型函数，允许 nullptr 表示缺少该方向。格式名由 builder 拥有。 */
  template <auto Encode, auto Decode>
  type_builder& serialization(std::string_view format) noexcept {
    if (committed_ || status_.failed())
      return *this;
    if (has_serialization_) {
      status_ = result::invalid_argument;
      return *this;
    }
    has_serialization_ = true;
    if constexpr (Encode == nullptr && Decode == nullptr) {
      status_ = result::invalid_argument;
      return *this;
    }
    try {
      format_ = format;
      if constexpr (Encode != nullptr)
        codec_.encode = [](const void* object, std::vector<std::byte>& output,
                           serialization_diagnostic& diagnostic) -> result {
          return std::invoke(Encode, *static_cast<const object_type*>(object), output, diagnostic);
        };
      if constexpr (Decode != nullptr)
        codec_.decode = [](std::span<const std::byte> bytes, void* object,
                           serialization_diagnostic& diagnostic) -> result {
          return std::invoke(Decode, bytes, *static_cast<object_type*>(object), diagnostic);
        };
    } catch (const std::bad_alloc&) {
      status_ = result::out_of_memory;
    } catch (...) {
      status_ = result::internal;
    }
    return *this;
  }
  [[nodiscard]] result commit() noexcept {
    if (committed_)
      return result::invalid_state;
    committed_ = true;
    if (status_.failed())
      return status_;
    try {
      std::vector<field_definition> fields;
      std::vector<property_binding> bindings;
      fields.reserve(entries_.size());
      bindings.reserve(entries_.size());
      for (const auto& item : entries_) {
        fields.push_back({item.id, item.guid, item.flags, item.name});
        bindings.push_back(item.binding);
      }
      codec_.format = format_;
      return register_type<object_type>(
          registry_, {.schema_version = version_, .fields = fields, .serialization = codec_},
          bindings);
    } catch (const std::bad_alloc&) {
      return result::out_of_memory;
    } catch (...) {
      return result::internal;
    }
  }

private:
  template <class Property>
  type_builder& add(field_id id, std::string_view name, field_flags flags,
                    property_binding binding) noexcept {
    if (committed_ || status_.failed())
      return *this;
    try {
      entries_.push_back({id, std::string(name), type_identity<Property>::guid(), flags, binding});
    } catch (const std::bad_alloc&) {
      status_ = result::out_of_memory;
    } catch (...) {
      status_ = result::internal;
    }
    return *this;
  }
  serialization_binding codec_{};
  std::string format_;
  bool has_serialization_ = false;
  registry_ref registry_;
  std::vector<entry> entries_;
  std::uint32_t version_ = 1;
  result status_ = result::success;
  bool committed_ = false;
};
}
