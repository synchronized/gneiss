// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/traits/type_identity.hpp"
#include "engine/core/type_id.hpp"
namespace gneiss::engine::meta {
/** 可反射多态对象；不承担引用计数，具体对象由 ref 或其他拥有者管理。 */
class object {
public:
  static constexpr std::string_view type_name = "meta.object";
  static constexpr engine::guid type_guid =
      engine::guid::literal("97430000-0000-0000-0000-000000000000");
  virtual ~object() = default;
  virtual engine::type_id dynamic_type() const noexcept = 0;
  virtual engine::guid dynamic_guid() const noexcept = 0;
  virtual const void* dynamic_address() const noexcept = 0;
};
/** 为具体类型提供实际身份与完整对象地址；派生类型必须重新使用此模板。 */
template <class T, class Base = object> class object_type : public Base {
public:
  engine::type_id dynamic_type() const noexcept override { return runtime_type_of<T>(); }
  engine::guid dynamic_guid() const noexcept override { return type_identity<T>::guid(); }
  const void* dynamic_address() const noexcept override { return static_cast<const T*>(this); }
};
}
