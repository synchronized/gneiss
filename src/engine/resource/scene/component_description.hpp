// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/core/traits/type_identity.hpp"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
namespace gneiss::engine {
/** 自有、只读的组件描述。复制共享不可变内容，编辑须构造新条目，不共享可变指针。 */
class component_description final {
public:
  template <class T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, component_description>)
  component_description(T object)
      : guid_(type_identity<T>::guid()), type_(runtime_type_of<T>()),
        object_(std::make_shared<const T>(std::move(object))) {}
  [[nodiscard]] engine::guid guid() const noexcept { return guid_; }
  template <class T> [[nodiscard]] const T* get() const noexcept {
    return type_ == runtime_type_of<T>() ? static_cast<const T*>(object_.get()) : nullptr;
  }
  [[nodiscard]] static result<component_description, meta::serialization_diagnostic>
  decode(engine::guid guid, std::span<const std::byte> bytes) noexcept try {
    auto type = meta::describe_type(guid);
    if (!type)
      return meta::serialization_diagnostic{.error = type.error()};
    if (!type->type_id)
      return meta::serialization_diagnostic{.error = error::unsupported};
    auto object = meta::deserialize_object(type->type_id, bytes);
    if (!object)
      return std::move(object).error();
    if (!*object)
      return meta::serialization_diagnostic{.error = error::internal};
    return component_description(guid, type->type_id, std::move(*object));
  } catch (const std::bad_alloc&) {
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  } catch (...) {
    return meta::serialization_diagnostic{.error = error::internal};
  }
  [[nodiscard]] result<std::vector<std::byte>, meta::serialization_diagnostic>
  encode() const noexcept try {
    auto type = meta::describe_type(guid_);
    if (!type)
      return meta::serialization_diagnostic{.error = type.error()};
    if (type->type_id != type_)
      return meta::serialization_diagnostic{.error = error::invalid_argument};
    return meta::serialize_object(type_, object_.get());
  } catch (const std::bad_alloc&) {
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  } catch (...) {
    return meta::serialization_diagnostic{.error = error::internal};
  }

  /** 只读借用；视图不能超过本描述及其共享内容的寿命。 */
  result<meta::instance> view() const noexcept {
    auto registry = meta::default_registry();
    if (!registry)
      return registry.error();
    return meta::instance::from_object(*registry, type_, object_.get());
  }

private:
  component_description(engine::guid guid, engine::type_id type, std::shared_ptr<const void> object)
      : guid_(guid), type_(type), object_(std::move(object)) {}
  engine::guid guid_;
  engine::type_id type_;
  std::shared_ptr<const void> object_;
};
/** 每个描述类型至多一项；set 替换整项，复制列表后修改不会影响原列表。 */
class component_descriptions final {
public:
  component_descriptions() = default;
  component_descriptions(std::initializer_list<component_description> values) {
    for (const auto& value : values) {
      if (find(value.guid()))
        throw std::invalid_argument("重复组件描述类型");
      values_.push_back(value);
    }
  }
  [[nodiscard]] const component_description* find(guid type) const noexcept {
    auto found = std::ranges::find(values_, type, &component_description::guid);
    return found == values_.end() ? nullptr : &*found;
  }
  template <class T> [[nodiscard]] const T* get() const noexcept {
    const auto* value = find(type_identity<T>::guid());
    return value ? value->template get<T>() : nullptr;
  }
  void set(component_description value) {
    auto found = std::ranges::find(values_, value.guid(), &component_description::guid);
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
    set(component_description(std::move(copy)));
    return true;
  }
  template <class T> void erase() {
    std::erase_if(values_, [](const auto& v) { return v.guid() == type_identity<T>::guid(); });
  }
  [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
  auto begin() const noexcept { return values_.begin(); }
  auto end() const noexcept { return values_.end(); }

private:
  std::vector<component_description> values_;
};
}
