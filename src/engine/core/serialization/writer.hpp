// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/core/value.hpp"
namespace gneiss::engine::serialization {
class context;
/** 后端拥有节点；句柄只能用于创建它的 writer，不能在 writer 销毁后保存。 */
class writer {
public:
  using node = void*;
  virtual ~writer() = default;
  virtual node object() = 0;
  virtual node array() = 0;
  virtual node null() = 0;
  virtual node string(std::string_view) = 0;
  virtual result<node, meta::serialization_diagnostic> scalar(const value&) = 0;
  virtual bool member(node object, std::string_view key, node value) = 0;
  virtual bool append(node array, node value) = 0;
  virtual bool is_object(node) const = 0;
  virtual bool is_null(node) const = 0;
  virtual bool root(node) = 0;
  /** 格式专用扩展；普通后端仅实现结构与标量操作即可。 */
  virtual bool has_custom(const meta::serialization_binding&) const { return false; }
  virtual result<node, meta::serialization_diagnostic>
  custom(const context&, type_id, const meta::serialization_binding&, const void*) {
    return meta::serialization_diagnostic{.error = error::unsupported};
  }
};
}
