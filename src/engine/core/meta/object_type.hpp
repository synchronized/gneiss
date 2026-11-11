// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/detail/type_ops.hpp"
#include "engine/core/meta/object.hpp"
namespace gneiss::meta {
/** 为具体类型提供静态能力与完整对象地址；派生类型必须重新使用此模板。 */
template <class T, class Base = object> class object_type : public Base {
public:
  const void* dynamic_address() const noexcept override { return static_cast<const T*>(this); }

protected:
  const detail::type_ops* dynamic_ops() const noexcept override { return &detail::ops_of<T>(); }
};
}
