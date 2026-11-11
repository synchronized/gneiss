// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/function/render/render_resource_data.hpp"

namespace gneiss::detail {
/** 所属逻辑线程访问的资源身份。空 data 表示未就绪；帧必须捕获不可变内容，不能跨线程读此记录。 */
template <typename Data> struct resource_identity {
  std::shared_ptr<const Data> data;
};
/** 材质身份保活依赖身份，纹理内容发布不需要重建材质。 */
template <> struct resource_identity<material_data> {
  std::shared_ptr<const material_data> data;
  std::array<std::shared_ptr<const resource_identity<texture_data>>, 5> textures{};
};
/** 某次捕获固定的内容；后续发布不会修改这些数据。纹理槽位与材质一致。 */
struct scene_resource_versions {
  std::shared_ptr<const mesh_data> mesh;
  std::shared_ptr<const material_data> material;
  std::array<std::shared_ptr<const texture_data>, 5> textures{};
};
struct scene_resource_pin {
  std::shared_ptr<const resource_identity<mesh_data>> mesh;
  std::shared_ptr<const resource_identity<material_data>> material;

  /** 所属线程捕获当前版本；空值由绘制侧按未就绪策略处理。 */
  scene_resource_versions capture() const {
    scene_resource_versions versions;
    versions.mesh = mesh ? mesh->data : nullptr;
    if (material) {
      versions.material = material->data;
      for (std::size_t index = 0; index < versions.textures.size(); ++index)
        if (material->textures[index])
          versions.textures[index] = material->textures[index]->data;
    }
    return versions;
  }
};
}
