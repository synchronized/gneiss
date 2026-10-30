// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/detail/type_access.hpp"
#include "engine/core/meta/registry.hpp"
#include <stdexcept>
#include <type_traits>
namespace gneiss::engine::meta {
template <bool Mutable, type_kind Kind> class container_view;
using sequence_view = container_view<false, type_kind::sequence>;
using mutable_sequence_view = container_view<true, type_kind::sequence>;
using associative_view = container_view<false, type_kind::associative>;
using mutable_associative_view = container_view<true, type_kind::associative>;
using optional_view = container_view<false, type_kind::optional>;
using mutable_optional_view = container_view<true, type_kind::optional>;
class mutable_instance;
/** 只读借用视图。持有注册表，不持有目标对象；父对象销毁、移动或成员地址失效后不可使用。
 * 基础属性保存访问位置，read_value 每次调用 getter 并返回自有副本，不借用 getter 临时值。
 * 共享视图不代表目标线程安全，调用方负责同步。 */
class GNEISS_API instance {
public:
  instance() = default;
  /** 类型擦除入口：调用方保证 object 的实际类型匹配 id，且地址在视图使用期间稳定。 */
  static result<instance> from_object(registry_ref registry, engine::type_id id,
                                      const void* object) noexcept;
  template <class T>
  static result<instance> from(const registry_ref& registry, T& object) noexcept {
    return from_access(registry, detail::access_of<std::remove_cv_t<T>>(), std::addressof(object));
  }
  template <class T> static result<instance> from(T& object) noexcept {
    auto registry = default_registry();
    if (!registry)
      return registry.error();
    return from(*registry, object);
  }
  template <class T>
    requires(!std::is_lvalue_reference_v<T>)
  static result<instance> from(const registry_ref&, T&&) = delete;
  template <class T>
    requires(!std::is_lvalue_reference_v<T>)
  static result<instance> from(T&&) = delete;
  [[nodiscard]] type_kind kind() const noexcept { return kind_; }
  [[nodiscard]] value_kind scalar_kind() const noexcept { return scalar_kind_; }
  [[nodiscard]] engine::guid guid() const noexcept { return type_; }
  [[nodiscard]] engine::type_id type_id() const noexcept { return cpp_type_; }
  [[nodiscard]] result<sequence_view> as_sequence() const noexcept;
  [[nodiscard]] result<associative_view> as_associative() const noexcept;
  [[nodiscard]] result<optional_view> as_optional() const noexcept;
  [[nodiscard]] result<std::vector<property_info>> properties() const noexcept;
  [[nodiscard]] result<instance> property(property_id id) const noexcept;
  [[nodiscard]] result<instance> property(std::string_view name) const noexcept;
  [[nodiscard]] result<value> read_value() const noexcept;
  /** 仅对象视图有稳定对象地址；基础属性（含 getter）返回 unsupported。 */
  [[nodiscard]] result<const void*> object_data() const noexcept;

private:
  friend class mutable_instance;
  template <bool, type_kind> friend class container_view;
  static result<instance> from_access(registry_ref, const detail::type_access&,
                                      const void*) noexcept;
  const detail::type_access* access_{};
  registry_ref registry_;
  engine::guid type_{};
  engine::type_id cpp_type_{};
  type_kind kind_ = type_kind::invalid;
  value_kind scalar_kind_ = value_kind::invalid;
  const void* data_{};
  const void* parent_{};
  engine::guid parent_type_{};
  property_id property_{};
};
/** 显式可写借用视图。setter 校验规则保持不变，不自动回滚外部副作用。
 * 修改完整对象后，业务方仍需执行类型校验；不能假定单字段修改保持所有跨字段约束。 */
class GNEISS_API mutable_instance {
public:
  mutable_instance() = default;
  static result<mutable_instance> from_object(registry_ref registry, engine::type_id id,
                                              void* object) noexcept;
  template <class T>
    requires(!std::is_const_v<T>)
  static result<mutable_instance> from(const registry_ref& registry, T& object) noexcept {
    return from_access(registry, detail::access_of<T>(), std::addressof(object));
  }
  template <class T>
    requires(!std::is_const_v<T>)
  static result<mutable_instance> from(T& object) noexcept {
    auto registry = default_registry();
    if (!registry)
      return registry.error();
    return from(*registry, object);
  }
  [[nodiscard]] result<mutable_sequence_view> as_sequence() const noexcept;
  [[nodiscard]] result<mutable_associative_view> as_associative() const noexcept;
  [[nodiscard]] result<mutable_optional_view> as_optional() const noexcept;
  [[nodiscard]] instance as_const() const noexcept { return view_; }
  [[nodiscard]] type_kind kind() const noexcept { return view_.kind(); }
  [[nodiscard]] result<mutable_instance> property(property_id id) const noexcept;
  [[nodiscard]] result<mutable_instance> property(std::string_view name) const noexcept;
  [[nodiscard]] result<value> read_value() const noexcept { return view_.read_value(); }
  [[nodiscard]] error write_value(const value& input) const noexcept;
  [[nodiscard]] result<void*> object_data() const noexcept;
  /** 同类型复制赋值；不提供回滚或业务校验，调用方应修改候选副本。 */
  [[nodiscard]] error assign(const instance& source) const noexcept;
  /** 解码专用移动赋值；source 类型须匹配，成功后其内容已移出。 */
  [[nodiscard]] error assign_object(engine::type_id id, void* source) const noexcept;

private:
  template <bool, type_kind> friend class container_view;
  static result<mutable_instance> from_access(registry_ref, const detail::type_access&,
                                              void*) noexcept;
  instance view_;
  void* data_{};
  void* parent_{};
};

/** 容器借用视图。结构修改后必须重新取得元素视图；键列表是自有快照。
 * optional 的 ensure 保留已有值；sequence 的 resize 按原生 vector 规则保留前缀。
 * 修改失败不提供事务回滚；调用方负责同步和目标寿命。 */
template <bool Mutable, type_kind Kind> class container_view {
  using target_type = std::conditional_t<Mutable, mutable_instance, instance>;
  friend class instance;
  friend class mutable_instance;
  explicit container_view(target_type target) : target_(std::move(target)) {}
  instance readonly() const noexcept {
    if constexpr (Mutable)
      return target_.as_const();
    else
      return target_;
  }
  target_type target_;

public:
  [[nodiscard]] result<std::size_t> size() const noexcept try {
    auto v = readonly();
    return v.access_->size(v.data_);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] result<bool> has_value() const noexcept
    requires(Kind == type_kind::optional)
  {
    auto n = size();
    if (!n)
      return n.error();
    return *n != 0;
  }
  [[nodiscard]] result<target_type> at(std::size_t index) const noexcept
    requires(Kind == type_kind::sequence || Kind == type_kind::optional)
  try {
    auto v = readonly();
    if (index >= v.access_->size(v.data_))
      return error::not_found;
    if constexpr (Mutable)
      return mutable_instance::from_access(v.registry_, v.access_->element(),
                                           v.access_->at_mut(target_.data_, index));
    else
      return instance::from_access(v.registry_, v.access_->element(),
                                   v.access_->at(v.data_, index));
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] result<target_type> get() const noexcept
    requires(Kind == type_kind::optional)
  {
    return at(0);
  }
  [[nodiscard]] bool is_resizable() const noexcept
    requires(Kind == type_kind::sequence)
  {
    return readonly().access_->resize != nullptr;
  }
  [[nodiscard]] error resize(std::size_t count) const noexcept
    requires(Mutable && Kind == type_kind::sequence)
  try {
    auto v = readonly();
    if (!v.access_->resize)
      return count == v.access_->size(v.data_) ? error::success : error::invalid_argument;
    v.access_->resize(target_.data_, count);
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (const std::length_error&) {
    return error::invalid_argument;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] error reset() const noexcept
    requires(Mutable && Kind == type_kind::optional)
  try {
    readonly().access_->resize(target_.data_, 0);
    return error::success;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] result<target_type> ensure() const noexcept
    requires(Mutable && Kind == type_kind::optional)
  try {
    readonly().access_->resize(target_.data_, 1);
    return at(0);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] result<std::vector<std::string>> keys() const noexcept
    requires(Kind == type_kind::associative)
  try {
    auto v = readonly();
    auto keys = v.access_->keys(v.data_);
    for (const auto& key : keys)
      if (!valid_scalar(value{key}))
        return error::invalid_argument;
    return keys;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] result<target_type> find(std::string_view key) const noexcept
    requires(Kind == type_kind::associative)
  try {
    auto v = readonly();
    if constexpr (Mutable) {
      auto* p = v.access_->find_mut(target_.data_, key);
      if (!p)
        return error::not_found;
      return mutable_instance::from_access(v.registry_, v.access_->element(), p);
    } else {
      auto* p = v.access_->find(v.data_, key);
      if (!p)
        return error::not_found;
      return instance::from_access(v.registry_, v.access_->element(), p);
    }
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  /** 已存在时返回原值；不覆盖。 */
  [[nodiscard]] result<target_type> insert(std::string_view key) const noexcept
    requires(Mutable && Kind == type_kind::associative)
  try {
    if (!valid_scalar(value{std::string(key)}))
      return error::invalid_argument;
    auto v = readonly();
    return mutable_instance::from_access(v.registry_, v.access_->element(),
                                         v.access_->insert(target_.data_, key));
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] error erase(std::string_view key) const noexcept
    requires(Mutable && Kind == type_kind::associative)
  try {
    return readonly().access_->erase(target_.data_, key) ? error::success : error::not_found;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] error clear() const noexcept
    requires(Mutable && Kind == type_kind::associative)
  try {
    readonly().access_->clear(target_.data_);
    return error::success;
  } catch (...) {
    return error::internal;
  }
};
}
