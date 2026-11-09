// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/object.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/value.hpp"
#include "engine/core/traits/type_identity.hpp"
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>
namespace gneiss::engine {
/** 不可变组件描述的多态基类；编辑时复制具体类型并替换引用。 */
class component_description_base : public meta::object {
public:
  static constexpr std::string_view type_name = "component_description_base";
  static constexpr engine::guid type_guid =
      engine::guid::literal("8bbd9682-e941-46a2-936a-b4ad959b2546");
  /** 组件实例身份；新建生成，编辑与文件恢复保持。 */
  engine::guid uuid = engine::guid::generate();
  [[nodiscard]] engine::guid guid() const noexcept { return dynamic_type().guid(); }
  template <class T> const T* get() const noexcept {
    return dynamic_type().id() == runtime_type_of<T>() ? static_cast<const T*>(dynamic_address())
                                                       : nullptr;
  }
  result<meta::const_instance> view() const noexcept {
    return meta::const_instance::from_object(dynamic_type().id(), dynamic_address());
  }
};
using component_description = ref<const component_description_base>;
template <class T>
  requires std::derived_from<T, component_description_base>
component_description make_component_description(T value) {
  return make_ref<const T>(std::move(value));
}
/** 在独立对象上指定身份，不修改调用方持有的不可变描述。 */
inline result<component_description> with_component_identity(const component_description& source,
                                                             guid identity) noexcept {
  if (!source || !identity)
    return error::invalid_argument;
  if (source->uuid == identity)
    return source;
  auto encoded = serialization::json::encode_bytes(source);
  if (!encoded)
    return encoded.error().error;
  auto copied = serialization::json::decode<ref<component_description_base>>(*encoded);
  if (!copied)
    return copied.error().error;
  if (!*copied)
    return error::invalid_argument;
  (*copied)->uuid = identity;
  return component_description{std::move(*copied)};
}
/** 每个描述类型至多一项；替换保留实例身份，删除后新增生成新身份。 */
class component_descriptions final {
public:
  component_descriptions() = default;
  component_descriptions(std::initializer_list<component_description> values) {
    for (const auto& value : values) {
      if (!value || find(value->guid()))
        throw std::invalid_argument("重复组件描述类型");
      set(value);
    }
  }
  [[nodiscard]] const component_description* find(guid type) const noexcept {
    auto found = std::ranges::find(values_, type, [](const auto& v) { return v->guid(); });
    return found == values_.end() ? nullptr : &*found;
  }
  [[nodiscard]] guid identity(guid type) const noexcept {
    auto found = std::ranges::find(values_, type, [](const auto& v) { return v->guid(); });
    return found == values_.end() ? guid{} : (*found)->uuid;
  }
  [[nodiscard]] const component_description* find_instance(guid uuid) const noexcept {
    auto found = std::ranges::find(values_, uuid, [](const auto& value) { return value->uuid; });
    return found == values_.end() ? nullptr : &*found;
  }
  template <class T> [[nodiscard]] const T* get() const noexcept {
    const auto* value = find(type_identity<T>::guid());
    return value ? (*value)->template get<T>() : nullptr;
  }
  template <class T>
    requires std::derived_from<T, component_description_base>
  void set(T value) {
    if (auto existing = identity(value.guid()))
      value.uuid = existing;
    set(make_component_description(std::move(value)));
  }
  void set(component_description value) {
    if (!value)
      throw std::invalid_argument("组件描述不能为空");
    auto found = std::ranges::find(values_, value->guid(), [](const auto& v) { return v->guid(); });
    const auto identity = found == values_.end() ? value->uuid : (*found)->uuid;
    auto accepted = with_component_identity(value, identity ? identity : guid::generate());
    if (!accepted)
      throw std::runtime_error("无法复制组件描述身份");
    if (std::ranges::any_of(values_, [&](const auto& existing) {
          return existing->guid() != value->guid() && existing->uuid == (*accepted)->uuid;
        }))
      throw std::invalid_argument("重复组件实例身份");
    if (found == values_.end())
      values_.push_back(std::move(*accepted));
    else
      *found = std::move(*accepted);
  }
  /** 在独立候选上编辑并校验；失败不改变集合或原引用，不支持的捕获不回退到浅拷贝。 */
  template <class T, class F> [[nodiscard]] error update(F&& edit) noexcept try {
    const auto* source = get<T>();
    if (!source)
      return error::not_found;
    auto captured = serialization::capture_value(*source);
    if (!captured)
      return captured.error().error;
    auto candidate = serialization::restore_value<T>(*captured);
    if (!candidate)
      return candidate.error().error;
    if constexpr (std::is_void_v<std::invoke_result_t<F, T&>>) {
      std::invoke(std::forward<F>(edit), *candidate);
    } else {
      static_assert(std::same_as<std::invoke_result_t<F, T&>, error>);
      auto status = std::invoke(std::forward<F>(edit), *candidate);
      if (status.failed())
        return status;
    }
    // 回调可替换内嵌引用；提交前再冻结，校验新内容并隔离回调持有的别名。
    auto edited = serialization::capture_value(*candidate);
    if (!edited)
      return edited.error().error;
    auto accepted = serialization::restore_value<T>(*edited);
    if (!accepted)
      return accepted.error().error;
    set(make_component_description(std::move(*accepted)));
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  void erase(guid type) {
    std::erase_if(values_, [type](const auto& v) { return v->guid() == type; });
  }
  template <class T> void erase() { erase(type_identity<T>::guid()); }
  /** 只读借用；替换、增删后须重新获取借用。 */
  const std::vector<component_description>& entries() const noexcept { return values_; }
  /** 文件与版本恢复使用显式身份；拒绝 nil、重复 UUID 和重复类型。 */
  error replace(const std::vector<component_description>& values) noexcept try {
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (!values[i] || !values[i]->uuid)
        return error::invalid_argument;
      for (std::size_t j = 0; j < i; ++j)
        if (values[j]->uuid == values[i]->uuid || values[j]->guid() == values[i]->guid())
          return error::invalid_argument;
    }
    auto candidate = values;
    values_.swap(candidate);
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
  auto begin() const noexcept { return values_.begin(); }
  auto end() const noexcept { return values_.end(); }

private:
  std::vector<component_description> values_;
};
}
namespace gneiss::engine::meta {
template <> struct sequence_identity_traits<component_descriptions> {
  static constexpr bool enabled = true;
  static guid identity(const component_description& entry) noexcept {
    return entry ? entry->uuid : guid{};
  }
};
/** 只读序列访问；解码整组校验后替换，禁止绕过集合约束修改元素。 */
template <> struct sequence_traits<component_descriptions> {
  static constexpr bool enabled = true;
  using element = component_description;
  static std::size_t size(const component_descriptions& value) { return value.size(); }
  static const element* at(const component_descriptions& value, std::size_t index) {
    return &value.entries()[index];
  }
  static error replace(component_descriptions& target, const std::vector<element>& values) {
    return target.replace(values);
  }
};
}
