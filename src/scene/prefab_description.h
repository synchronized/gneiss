// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_DESCRIPTION_H_
#define GNEISS_SCENE_PREFAB_DESCRIPTION_H_

#include "scene/prefab_property_override.h"
#include "scene/scene_description.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::scene_internal {

struct prefab_description final {
  std::uint32_t source_schema_version = 0;
  std::string uuid;
  std::vector<object_description> objects;
  std::vector<std::string> dependencies;
  std::string author_json;
};

/** 解析版本化 Prefab 作者文件；Prefab 必须且只能包含一个根节点。 */
[[nodiscard]] gneiss_result parse_prefab_description(std::string_view json,
                                                     prefab_description& out_prefab,
                                                     scene_diagnostic& out_diagnostic) noexcept;

[[nodiscard]] gneiss_result
load_prefab_description(const asset_internal::virtual_file_system& file_system,
                        std::string_view uri, prefab_description& out_prefab,
                        scene_diagnostic& out_diagnostic) noexcept;

} // namespace gneiss::scene_internal

#endif
