// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_DESCRIPTION_HPP_
#define GNEISS_SCENE_PREFAB_DESCRIPTION_HPP_

#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/scene/game_object_description.hpp"
#include "engine/resource/scene/scene_description.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

struct prefab_description final {
  inline static constexpr std::string_view type_name = "prefab_description";
  inline static constexpr engine::guid type_guid{
      engine::guid::literal("4b91d830-28b7-4257-815f-673a9e6b7012")};
  inline static constexpr std::size_t maximum_source_bytes = 16U * 1024U * 1024U;
  guid uuid;
  /** 对象树入口；必须指向唯一无父对象。 */
  guid root_uuid;
  std::vector<game_object_description> objects;
};

/** 共享只读来源描述；实例的覆写数据另行持有。 */
using prefab_asset_ref = asset_ref<const prefab_description>;

} // namespace gneiss::engine

#endif
