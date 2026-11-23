// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/asset/detail/asset_construction.hpp"
#include <gneiss/engine/function/render/service.hpp>
namespace gneiss::detail {
// 材质字段仍由通用对象解码器处理；此处只负责所属线程的内容采用。
error asset_construction::adopt(asset_manager& assets, const material_description& description,
                                const ref<const gneiss::material>& target,
                                std::array<ref<const gneiss::texture>, 5> dependencies) {
  if (!assets.construction() || assets.stop_requested())
    return error::invalid_state;
  if (!assets.owns(target))
    return error::invalid_argument;
  for (std::size_t slot = 0; slot < dependencies.size(); ++slot) {
    if (!description.texture_uris[slot].empty() &&
        (!dependencies[slot] || resource_uri(dependencies[slot]) != description.texture_uris[slot]))
      return error::invalid_argument;
  }
  auto prepared = assets.construction()->prepare_material(description.parameters, dependencies);
  if (!prepared)
    return prepared.error();
  auto data = std::move(*prepared);
  auto status =
      assets.construction()->update_material(target, std::move(data), std::move(dependencies));
  return status ? error::success : status.error();
}
}
