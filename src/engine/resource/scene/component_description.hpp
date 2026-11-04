// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/object.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/core/traits/type_identity.hpp"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
namespace gneiss::engine {
/** 不可变组件描述的多态基类；编辑时复制具体类型并替换引用。 */
class component_description_base : public meta::object {
public:
  static constexpr std::string_view type_name = "component_description_base";
  static constexpr engine::guid type_guid = engine::guid::from_bytes({0x97, 0x20});
  [[nodiscard]] engine::guid guid() const noexcept { return dynamic_guid(); }
  template <class T> const T* get() const noexcept {
    return dynamic_type() == runtime_type_of<T>() ? static_cast<const T*>(dynamic_address())
                                                  : nullptr;
  }
  result<meta::instance> view() const noexcept {
    auto registry = meta::default_registry();
    if (!registry)
      return registry.error();
    return meta::instance::from_object(*registry, dynamic_type(), dynamic_address());
  }
};
using component_description = ref<const component_description_base>;
template <class T>
  requires std::derived_from<T, component_description_base>
component_description make_component_description(T value) {
  return make_ref<const T>(std::move(value));
}
/** 每个描述类型至多一项；set 替换整项，复制列表后修改不会影响原列表。 */
class component_descriptions final {
public:
  component_descriptions() = default;
  component_descriptions(std::initializer_list<component_description> values) {
    for (const auto& value : values) {
      if (!value || find(value->guid()))
        throw std::invalid_argument("重复组件描述类型");
      values_.push_back(value);
    }
  }
  [[nodiscard]] const component_description* find(guid type) const noexcept {
    auto found = std::ranges::find(values_, type, [](const auto& v) { return v->guid(); });
    return found == values_.end() ? nullptr : &*found;
  }
  template <class T> [[nodiscard]] const T* get() const noexcept {
    const auto* value = find(type_identity<T>::guid());
    return value ? (*value)->template get<T>() : nullptr;
  }
  template <class T>
    requires std::derived_from<T, component_description_base>
  void set(T value) {
    set(make_component_description(std::move(value)));
  }
  void set(component_description value) {
    if (!value)
      throw std::invalid_argument("组件描述不能为空");
    auto found = std::ranges::find(values_, value->guid(), [](const auto& v) { return v->guid(); });
    if (found == values_.end())
      values_.push_back(std::move(value));
    else
      *found = std::move(value);
  }
  template <class T, class F> bool update(F&& edit) {
    const auto* value = get<T>();
    if (!value)
      return false;
    auto copy = *value;
    edit(copy);
    set(make_component_description(std::move(copy)));
    return true;
  }
  void erase(guid type) {
    std::erase_if(values_, [type](const auto& v) { return v->guid() == type; });
  }
  template <class T> void erase() {
    std::erase_if(values_, [](const auto& v) { return v->guid() == type_identity<T>::guid(); });
  }
  [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
  auto begin() const noexcept { return values_.begin(); }
  auto end() const noexcept { return values_.end(); }

private:
  std::vector<component_description> values_;
};
}
