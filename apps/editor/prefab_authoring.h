// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_PREFAB_AUTHORING_H_
#define GNEISS_APPS_EDITOR_PREFAB_AUTHORING_H_

#include "author_transaction.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::editor {

struct create_prefab_author_request final {
  std::string_view scene_path;
  std::string_view prefab_path;
  std::string_view prefab_uri;
  std::string_view root_uuid;
  std::string_view prefab_uuid;
  std::string_view instance_uuid;
};

struct apply_prefab_author_request final {
  std::string_view scene_path;
  std::string_view prefab_path;
  std::string_view prefab_uri;
  std::string_view instance_uuid;
};

struct apply_prefab_author_plan final {
  std::vector<author_document_change> changes;
  std::vector<std::string> affected_instance_uuids;
};

struct unpack_prefab_uuid_mapping final {
  std::string_view source_node_uuid;
  std::string_view target_node_uuid;
};

struct unpack_prefab_author_request final {
  std::string_view scene_path;
  std::string_view prefab_uri;
  std::string_view instance_uuid;
  std::string_view instance_root_uuid;
  std::span<const unpack_prefab_uuid_mapping> node_mappings;
};

/**
 * 将普通场景子树转换为 Prefab 来源和场景实例两项作者文档变更。
 *
 * 该函数只准备事务，不写文件。Prefab 目标必须尚不存在；调用方应把结果交给作者事务提交。
 */
[[nodiscard]] result
prepare_create_prefab(std::string_view scene_json, const create_prefab_author_request& request,
                      std::vector<author_document_change>& out_changes) noexcept;

/** 把指定实例已有的 Transform 字段覆盖应用到 Prefab 来源，并清除该实例的已提交覆盖。 */
[[nodiscard]] result prepare_apply_prefab(std::string_view scene_json, std::string_view prefab_json,
                                          const apply_prefab_author_request& request,
                                          apply_prefab_author_plan& out_plan) noexcept;

/** 将指定 Prefab 实例的当前可见投影物化为拥有新 UUID 的普通场景子树。 */
[[nodiscard]] result
prepare_unpack_prefab(std::string_view scene_json, std::string_view prefab_json,
                      const unpack_prefab_author_request& request,
                      std::vector<author_document_change>& out_changes) noexcept;

/** 为已提交的作者文档变更生成严格反向事务，用于 Undo。 */
[[nodiscard]] result
invert_author_document_changes(std::span<const author_document_change> changes,
                               std::vector<author_document_change>& out_changes) noexcept;

} // namespace gneiss::editor

#endif
