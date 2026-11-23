// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/resource/export.hpp>
#include <gneiss/engine/core/data/value.hpp>
#include <gneiss/engine/resource/scene/prefab_description.hpp>
namespace gneiss {
/** 组件身份独立于类型，内容按反射冻结。 */
struct component_record {
  guid uuid;
  guid type;
  value properties;
  bool operator==(const component_record&) const = default;
};
struct component_override_record {
  guid source_node_uuid;
  guid component_uuid;
  guid type;
  std::optional<component_record> added;
  GNEISS_RESOURCE_API bool operator==(const component_override_record&) const;
};
struct prefab_reference_record {
  std::string uri;
  std::vector<prefab_property_override> overrides;
  std::vector<component_override_record> components;
  GNEISS_RESOURCE_API bool operator==(const prefab_reference_record&) const;
};
/** 场景领域字段显式表达，不能通过通用属性路径修改身份或父子关系。 */
struct object_record {
  guid uuid;
  object_address parent;
  std::string name;
  math::transform transform;
  std::vector<component_record> components;
  std::optional<prefab_reference_record> prefab;
  GNEISS_RESOURCE_API bool operator==(const object_record&) const;
};
struct scene_record_data {
  guid uuid;
  std::vector<object_record> objects;
  bool operator==(const scene_record_data&) const = default;
};
struct prefab_record_data {
  guid uuid;
  guid root_uuid;
  std::vector<object_record> objects;
  bool operator==(const prefab_record_data&) const = default;
};
/** 只读领域版本；复制共享内容，构造冻结输入，不持有可写组件引用。 */
using scene_record = immutable_value_storage<scene_record_data>;
using prefab_record = immutable_value_storage<prefab_record_data>;
[[nodiscard]] GNEISS_RESOURCE_API result<scene_record> capture_scene_record(const scene_description&);
[[nodiscard]] GNEISS_RESOURCE_API result<prefab_record> capture_prefab_record(const prefab_description&);
[[nodiscard]] GNEISS_RESOURCE_API result<scene_description> restore_scene_record(const scene_record&);
[[nodiscard]] GNEISS_RESOURCE_API result<prefab_description> restore_prefab_record(const prefab_record&);
/** 显式对象身份映射；不用于组件类型、Prefab 来源节点或普通 GUID 字段。 */
struct uuid_mapping {
  guid source_uuid;
  guid target_uuid;
};
/** 复制领域内容：完整映射对象/父关系及指向复制对象的引用。
 * 跨文档引用、子树外引用及资产身份保持原值；映射必须覆盖全部显式对象。
 * 不创建运行时对象，不修改输入，不为无身份容器生成身份。
 */
[[nodiscard]] GNEISS_RESOURCE_API result<scene_record>
remap_scene_record(const scene_record&, guid target_document, std::span<const uuid_mapping>);

/** 借用已知文档的对象目录；调用期间有效，不触发外部加载。 */
struct document_object_view {
  guid document;
  std::span<const object_record> objects;
};
struct object_reference_issue {
  guid document;
  guid object;
  guid component;
  object_reference_value target;
  error status;
};
/** 检查组件及覆盖中的对象引用。缺失目标为 not_found，未知外部文档为 not_ready。
 * 空引用合法；诊断不修改内容，不阻止编辑、删除或保存。
 */
[[nodiscard]] GNEISS_RESOURCE_API result<std::vector<object_reference_issue>>
inspect_object_references(document_object_view,
                          std::span<const document_object_view> external = {});
}
