// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_PREFAB_AUTHORING_H_
#define GNEISS_APPS_EDITOR_PREFAB_AUTHORING_H_

#include "commands/author_document_change.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/resource/scene/prefab_description.hpp"
#include "engine/resource/scene/scene_description.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::editor {

struct create_prefab_author_request final {
  std::string_view scene_path;
  std::string_view prefab_path;
  std::string_view prefab_uri;
  gneiss::engine::guid root_uuid;
  gneiss::engine::guid prefab_uuid;
  gneiss::engine::guid instance_uuid;
};

struct apply_prefab_author_request final {
  std::string_view scene_path;
  std::string_view prefab_path;
  std::string_view prefab_uri;
  gneiss::engine::guid instance_uuid;
};

struct apply_prefab_author_plan final {
  std::vector<author_document_change> changes;
  std::vector<engine::guid> affected_instance_uuids;
};

struct unpack_prefab_uuid_mapping final {
  gneiss::engine::guid source_node_uuid;
  gneiss::engine::guid target_node_uuid;
};

struct unpack_prefab_author_request final {
  std::string_view scene_path;
  std::string_view prefab_uri;
  gneiss::engine::guid instance_uuid;
  std::span<const unpack_prefab_uuid_mapping> node_mappings;
};

/** 临时显示项；key 使用来源对象/组件/属性身份，不使用列表下标。 */
struct prefab_override_entry {
  engine::prefab_property_override_key key;
  bool component = false;
  bool added = false;
  std::string label;
};
[[nodiscard]] engine::result<std::vector<prefab_override_entry>>
describe_prefab_overrides(const engine::prefab_reference_description&,
                          const engine::prefab_description&) noexcept;
/** 空 entry 恢复整个实例；不写来源，生成父场景内存变更供快照历史使用。 */
[[nodiscard]] engine::result<std::vector<author_document_change>>
prepare_restore_prefab(std::string_view scene_json, std::string_view scene_path,
                       engine::guid instance,
                       const prefab_override_entry* entry = nullptr) noexcept;

/**
 * 将普通场景子树转换为 Prefab 来源和场景实例两项作者文档变更。
 *
 * 该函数只准备内存编辑计划，不写文件。调用方独立保存新来源，再采用父场景内存变更。
 */
[[nodiscard]] gneiss::engine::error
prepare_create_prefab(std::string_view scene_json, const create_prefab_author_request& request,
                      std::vector<author_document_change>& out_changes) noexcept;

/** 应用完整组件/属性覆盖并检查当前场景全部同源实例；只生成计划，不写文件。 */
[[nodiscard]] gneiss::engine::error
prepare_apply_prefab(std::string_view scene_json, std::string_view prefab_json,
                     const apply_prefab_author_request& request, apply_prefab_author_plan& out_plan,
                     engine::scene_diagnostic* diagnostic = nullptr) noexcept;

/** 将指定 Prefab 实例的当前可见投影物化为拥有新 UUID 的普通场景子树。 */
[[nodiscard]] gneiss::engine::error
prepare_unpack_prefab(std::string_view scene_json, std::string_view prefab_json,
                      const unpack_prefab_author_request& request,
                      std::vector<author_document_change>& out_changes,
                      engine::scene_diagnostic* diagnostic = nullptr) noexcept;

} // namespace gneiss::editor

#endif
