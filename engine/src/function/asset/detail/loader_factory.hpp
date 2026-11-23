// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/export.hpp>
#include <gneiss/engine/function/asset/asset_loader.hpp>
#include <gneiss/engine/resource/asset_decoder.hpp>
#include <gneiss/engine/resource/environment.hpp>
#include <gneiss/engine/resource/input/action_map.hpp>
#include <gneiss/engine/resource/mesh/mesh_data.hpp>
#include <gneiss/engine/resource/prefab.hpp>
#include <gneiss/engine/resource/scene.hpp>
namespace gneiss::detail {
GNEISS_ENGINE_API std::shared_ptr<asset_loader> make_mesh_loader(std::string uri);
GNEISS_ENGINE_API std::shared_ptr<asset_loader>
make_texture_loader(std::string uri, const asset_load_options& options);
GNEISS_ENGINE_API std::shared_ptr<asset_loader>
make_object_loader(std::string uri, std::shared_ptr<const asset_decoder> decoder);

GNEISS_ENGINE_API std::shared_ptr<asset_loader>
    make_action_map_loader(asset_identity, std::shared_ptr<const asset_decoder>);

GNEISS_ENGINE_API std::shared_ptr<asset_loader> make_scene_loader(asset_identity, bool prefab,
                                                                  bool reload);

std::shared_ptr<asset_loader> make_environment_loader(asset_identity);

template <class Resolve>
result<std::shared_ptr<asset_loader>>
make_asset_loader(type_id type, std::string uri, asset_load_options options, Resolve resolve,
                  std::shared_ptr<const files::source_domain> domain) {
  if (type == runtime_type_of<mesh_data>())
    return make_mesh_loader(std::move(uri));
  if (type == runtime_type_of<texture_data>())
    return make_texture_loader(std::move(uri), options);
  if (type == runtime_type_of<scene>() || type == runtime_type_of<prefab>())
    return make_scene_loader({std::move(domain), std::move(uri), 0},
                             type == runtime_type_of<prefab>(), options.reload);
  if (type == runtime_type_of<environment>())
    return make_environment_loader({std::move(domain), std::move(uri), 0});
  auto decoder = resolve();
  if (!decoder)
    return decoder.error();
  if (type == runtime_type_of<gneiss::input::action_map>())
    return make_action_map_loader({std::move(domain), std::move(uri), 0}, std::move(*decoder));
  return make_object_loader(std::move(uri), std::move(*decoder));
}
}
