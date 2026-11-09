// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/registry.hpp"
#include "engine/core/ref.hpp"
#include "engine/core/result.hpp"
#include "engine/core/serialization/diagnostic.hpp"
#include "engine/core/value.hpp"
namespace gneiss::engine::serialization {
class context;
class reader;
class writer;
/** 类型的持久化策略；回调借用对象及节点，解码返回独立所有者。
 * 回调须支持不同对象的并发操作，所属模块须在注册表使用期间存活。 */
struct binding {
  /** 引用位置专用编解码；不改变目标对象自身的持久化策略，空引用由框架处理。 */
  result<void*, diagnostic> (*encode_reference)(const context&, writer&, const void*){};
  result<ref<void>, diagnostic> (*decode_reference)(const context&, reader&, const void*){};
  bool reflected = false;
  bool reject_unknown_fields = false;
  /** 声明引用保存实际派生类型；不由继承关系自动开启。 */
  bool polymorphic = false;
  result<void*, diagnostic> (*encode)(const context&, writer&, const void*){};
  result<ref<void>, diagnostic> (*decode)(const context&, reader&, const void*){};
  /** 编码前和解码完成后校验；不修改对象。 */
  diagnostic (*validate)(const void*){};
  /** 外部资产引用的内存捕获；与文件编码分离，不读取文件。 */
  result<asset_reference_value> (*capture_reference)(engine::guid, const void*){};
};
/** 迁移期策略查询；存储属于 Meta 的通用扩展，解释属于 Serialization。 */
inline result<binding> find_serialization(const meta::registry_ref& registry, type_id id) noexcept {
  auto type = meta::find_type(registry, id);
  if (!type)
    return type.error();
  const auto* policy = type->extension<binding>();
  auto object = meta::find_object_ops(registry, id);
  if ((!policy || (!policy->encode_reference && !policy->decode_reference && !policy->encode &&
                   !policy->decode && !policy->reflected && !policy->polymorphic)) &&
      (!object || !object->base_type))
    return error::unsupported;
  return policy ? *policy : binding{};
}
inline result<binding> find_serialization(type_id id) noexcept {
  auto type = meta::find_type(id);
  if (!type)
    return type.error();
  const auto* policy = type->extension<binding>();
  auto object = meta::find_object_ops(id);
  if ((!policy || (!policy->encode_reference && !policy->decode_reference && !policy->encode &&
                   !policy->decode && !policy->reflected && !policy->polymorphic)) &&
      (!object || !object->base_type))
    return error::unsupported;
  return policy ? *policy : binding{};
}

}
