// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/meta/ops/object_ops.hpp>
#include <gneiss/engine/core/meta/property.hpp>
#include <gneiss/engine/core/meta/type_types.hpp>

namespace gneiss::meta {
struct property_definition {
  property_id id{};
  gneiss::guid type_guid{};
  property_access access = property_access::read_write;
  std::string_view name{};
  meta::metadata extensions{};
};
/** 回调借用实际类型的对象指针；调用方保证对象存活、类型正确和线程访问合法。
 * setter 必须先校验再修改，失败保持对象；注册表不复制对象或回滚任意回调副作用。
 * 上下文和对象借用；getter 输出拥有数据的 value，成功时 kind 必须匹配登记类型。
 * getter 失败原样传播；成功但数值/文本内容非法返回 invalid_argument。
 * setter 只在调用期间借用输入 value。 */
struct property_binding {
  value_kind kind = value_kind::invalid;
  error (*getter)(void*, const void*, value&) = nullptr;
  error (*setter)(void*, void*, const value&) = nullptr;
  void* user_data{};
  gneiss::type_id object_type{};
  /** 必须返回非空的成员存储地址（可空引用返回引用本身的地址）；仅在父对象存活且地址
   * 稳定时有效。可写访问用于解码临时对象。 */
  const void* (*object_getter)(const void*){};
  void* (*object_mutator)(void*){};
  /** 自定义子类型解码成功后，将独立值移入父对象成员。 */
  void (*object_assign)(void*, void*){};
  /** 受约束属性只允许整项替换；失败须保持原对象，不提供可写成员地址。 */
  error (*object_setter)(void*, const void*){};
  /** 返回非空的独立候选；分配失败抛异常，不以空指针表达可恢复失败。 */
  ref<void> (*object_construct)(){};
  gneiss::type_id enum_type{};
  const type_ops* access{}; // 静态只读操作表，不持有对象。
};

struct type_definition {
  gneiss::guid guid{};
  /** 可空；非空时必须与 guid 一一对应，不持久化。 */
  gneiss::type_id type_id{};
  std::uint32_t schema_version{};
  std::string_view name{};
  std::span<const property_definition> properties{};
  object_ops object{};
  meta::metadata extensions{};
  /** 模板注册填充的静态能力；运行期定义可留空，不拥有描述或对象。 */
  const type_ops* native{};
};
/** 枚举注册输入；条目和扩展在提交时复制，不拥有实例。 */
struct enum_definition {
  gneiss::guid guid{};
  gneiss::type_id type_id{};
  std::uint32_t schema_version{};
  std::string_view name{};
  value_kind kind = value_kind::invalid;
  std::span<const enum_entry> entries{};
  meta::metadata extensions{};
  const type_ops* native{};
};
}
