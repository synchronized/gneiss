// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_DESCRIPTION_HPP_
#define GNEISS_SCENE_PREFAB_DESCRIPTION_HPP_

#include "engine/core/result.hpp"
#include "engine/core/type_guid.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/types/prefab_property_data.hpp"
#include "engine/resource/types/scene_description.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

struct prefab_description final {
  inline static constexpr std::string_view type_name = "prefab_description";
  inline static constexpr engine::type_guid type_guid{
      engine::guid::from_bytes({0x4b, 0x91, 0xd8, 0x30, 0x28, 0xb7, 0x42, 0x57, 0x81, 0x5f, 0x67,
                                0x3a, 0x9e, 0x6b, 0x70, 0x12})};
  inline static constexpr std::size_t maximum_source_bytes = 16U * 1024U * 1024U;
  std::uint32_t source_schema_version = 0;
  std::string uuid;
  std::vector<object_description> objects;
  std::vector<std::string> dependencies;
  std::string author_json;
};

/** 共享只读来源描述；实例的覆写数据另行持有。 */
using prefab_asset_ref = asset_ref<const prefab_description>;

/** 解析版本化 Prefab 作者文件；Prefab 必须且只能包含一个根节点。 */
[[nodiscard]] gneiss::engine::result
parse_prefab_description(std::string_view json, prefab_description& out_prefab,
                         scene_diagnostic& out_diagnostic) noexcept;

/** 输出 Prefab v1 作者 JSON；保留受支持文档中的未知字段。 */
[[nodiscard]] gneiss::engine::result serialize_prefab_description(const prefab_description& prefab,
                                                                  std::string& out_json) noexcept;

} // namespace gneiss::engine

#endif
