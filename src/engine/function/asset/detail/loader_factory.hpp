// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_loader.hpp"
#include "engine/resource/asset_decoder.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
namespace gneiss::engine::detail {
GNEISS_API std::shared_ptr<asset_loader> make_mesh_loader(std::string uri);
GNEISS_API std::shared_ptr<asset_loader> make_texture_loader(std::string uri,
                                                             const asset_load_options& options);
GNEISS_API std::shared_ptr<asset_loader>
make_object_loader(std::string uri, std::shared_ptr<const asset_decoder> decoder);

template <class Resolve>
result<std::shared_ptr<asset_loader>>
make_asset_loader(type_id type, std::string uri, asset_load_options options, Resolve resolve) {
  if (type == runtime_type_of<mesh_data>())
    return make_mesh_loader(std::move(uri));
  if (type == runtime_type_of<texture_data>())
    return make_texture_loader(std::move(uri), options);
  auto decoder = resolve();
  if (!decoder)
    return decoder.error();
  return make_object_loader(std::move(uri), std::move(*decoder));
}

}
