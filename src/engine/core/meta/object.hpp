// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/ops/ops_fwd.hpp"
#include "engine/core/meta/type.hpp"
namespace gneiss::meta {
/** 可反射多态对象；不承担引用计数，具体对象由 ref 或其他拥有者管理。 */
class object {
public:
  static constexpr std::string_view type_name = "meta.object";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("ba2a0ea8-d167-428d-895e-9dd517bdc907");
  virtual ~object() = default;
  /** 返回实际类型；未注册仍可查询静态能力，无静态描述时返回空句柄。 */
  [[nodiscard]] GNEISS_API meta::type dynamic_type() const;
  [[nodiscard]] GNEISS_API meta::type dynamic_type(const registry_ref&) const;
  virtual const void* dynamic_address() const noexcept = 0;

protected:
  virtual const detail::type_ops* dynamic_ops() const noexcept = 0;
};
}
