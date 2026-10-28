// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_DESCRIPTION_HPP_
#define GNEISS_SCENE_PREFAB_DESCRIPTION_HPP_

#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/scene/prefab_property_data.hpp"
#include "engine/resource/scene/scene_description.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

struct prefab_description final {
  inline static constexpr std::string_view type_name = "prefab_description";
  inline static constexpr engine::guid type_guid{
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

} // namespace gneiss::engine

#endif
