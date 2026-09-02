// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_DESCRIPTION_H_
#define GNEISS_SCENE_PREFAB_DESCRIPTION_H_

#include "scene/scene_description.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::scene_internal {

/** Prefab 实例中节点的稳定作者身份。 */
struct prefab_author_address final {
  std::string instance_uuid;
  std::string source_node_uuid;

  [[nodiscard]] bool operator==(const prefab_author_address&) const noexcept = default;
};

struct prefab_description final {
  std::uint32_t source_schema_version = 0;
  std::string uuid;
  std::vector<object_description> objects;
  std::string author_json;
};

/** 判断复合作者身份的两个 UUID 是否均为规范的小写形式。 */
[[nodiscard]] bool is_valid_prefab_author_address(const prefab_author_address& address) noexcept;

/** 解析版本化 Prefab 作者文件；Prefab 必须且只能包含一个根节点。 */
[[nodiscard]] gneiss_result parse_prefab_description(std::string_view json,
                                                     prefab_description& out_prefab,
                                                     scene_diagnostic& out_diagnostic) noexcept;

} // namespace gneiss::scene_internal

#endif
