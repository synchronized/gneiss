// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/ref.hpp"
#include "engine/core/result.hpp"
#include "engine/core/serialization/diagnostic.hpp"
namespace gneiss::engine::serialization {
class context;
class reader;
class writer;
/** 类型的持久化策略；回调借用对象及节点，解码返回独立所有者。
 * 回调须支持不同对象的并发操作，所属模块须在注册表使用期间存活。 */
struct binding {
  bool reflected = false;
  bool reject_unknown_fields = false;
  /** 声明引用保存实际派生类型；不由继承关系自动开启。 */
  bool polymorphic = false;
  result<void*, diagnostic> (*encode)(const context&, writer&, const void*){};
  result<ref<void>, diagnostic> (*decode)(const context&, reader&, const void*){};
  /** 编码前和解码完成后校验；不修改对象。 */
  diagnostic (*validate)(const void*){};
};
}
