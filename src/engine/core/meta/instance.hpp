// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/registry.hpp"
#include <type_traits>
namespace gneiss::engine::meta {
/** 当前视图支持的类别；容器能力后续扩展，不冒充已经可用。 */
enum class type_kind { invalid, scalar, object };
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
    return from_object(registry, runtime_type_of<std::remove_cv_t<T>>(), std::addressof(object));
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
  [[nodiscard]] result<std::vector<property_info>> properties() const noexcept;
  [[nodiscard]] result<instance> property(property_id id) const noexcept;
  [[nodiscard]] result<instance> property(std::string_view name) const noexcept;
  [[nodiscard]] result<value> read_value() const noexcept;
  /** 仅对象视图有稳定对象地址；基础属性（含 getter）返回 unsupported。 */
  [[nodiscard]] result<const void*> object_data() const noexcept;

private:
  friend class mutable_instance;
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
    return from_object(registry, runtime_type_of<T>(), std::addressof(object));
  }
  template <class T>
    requires(!std::is_const_v<T>)
  static result<mutable_instance> from(T& object) noexcept {
    auto registry = default_registry();
    if (!registry)
      return registry.error();
    return from(*registry, object);
  }
  [[nodiscard]] instance as_const() const noexcept { return view_; }
  [[nodiscard]] type_kind kind() const noexcept { return view_.kind(); }
  [[nodiscard]] result<mutable_instance> property(property_id id) const noexcept;
  [[nodiscard]] result<mutable_instance> property(std::string_view name) const noexcept;
  [[nodiscard]] result<value> read_value() const noexcept { return view_.read_value(); }
  [[nodiscard]] error write_value(const value& input) const noexcept;
  [[nodiscard]] result<void*> object_data() const noexcept;
  /** 仅嵌套对象成员支持整体赋值；source 必须指向 id 类型的可移动值，成功后其内容已移出。 */
  [[nodiscard]] error assign_object(engine::type_id id, void* source) const noexcept;

private:
  instance view_;
  void* data_{};
  void* parent_{};
};
}
