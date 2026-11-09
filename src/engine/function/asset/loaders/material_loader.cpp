// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/asset/detail/asset_construction.hpp"
#include "engine/function/render/render_service.hpp"
namespace gneiss::engine::detail {
// 材质字段仍由通用对象解码器处理；此处只负责所属线程的内容采用。
error asset_construction::adopt(asset_manager& assets, const material_description& description,
                                const ref<const engine::material>& target,
                                std::array<ref<const engine::texture>, 5> dependencies) {
  if (!assets.impl_ || assets.stop_requested())
    return error::invalid_state;
  if (!assets.owns(target))
    return error::invalid_argument;
  for (std::size_t slot = 0; slot < dependencies.size(); ++slot) {
    if (!description.texture_uris[slot].empty() &&
        (!dependencies[slot] || resource_uri(dependencies[slot]) != description.texture_uris[slot]))
      return error::invalid_argument;
  }
  auto prepared = assets.impl_->prepare_material(description.parameters, dependencies);
  if (!prepared)
    return prepared.error();
  auto data = std::move(*prepared);
  return assets.impl_->update_material(target, std::move(data), std::move(dependencies))
             ? error::success
             : error::out_of_memory;
}
}
