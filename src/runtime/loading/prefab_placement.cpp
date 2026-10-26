// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime/loading/prefab_placement.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/structural_diff.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include <algorithm>

namespace gneiss::runtime {
gneiss::engine::result prefab_placement::capture_step(
    runtime::application& application, std::string_view uuid, gneiss::scene_instance_id scene,
    std::shared_ptr<prefab_placement_snapshot>& snapshot, bool& complete) noexcept try {
  complete = false;
  auto* host = &application;
  const auto validation = host->is_owner_thread() && host->accepts_requests()
                              ? gneiss::engine::result::success
                              : gneiss::engine::result::invalid_state;
  if (validation != gneiss::engine::result::success) {
    return validation;
  }
  if (!snapshot) {
    auto candidate = std::make_shared<prefab_placement_snapshot>();
    auto result =
        host->scenes()->capture_prefab_header(scene, uuid, candidate->author, candidate->prefab);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    gneiss::engine::structural_diff order;
    result = gneiss::engine::build_structural_diff({}, candidate->prefab.get()->objects, order);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    candidate->order.reserve(order.added.size());
    for (const auto& entry : order.added) {
      candidate->order.push_back(entry.new_index);
    }
    candidate->bindings.reserve(candidate->order.size());
    snapshot = std::move(candidate);
  }
  for (unsigned step = 0; step < 64U && snapshot->bindings.size() < snapshot->order.size();
       ++step) {
    gneiss::engine::scene_mesh_binding binding;
    const auto result =
        host->scenes()->capture_prefab_binding(scene, uuid, snapshot->bindings.size(), binding);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    snapshot->bindings.push_back(std::move(binding));
  }
  complete = snapshot->bindings.size() == snapshot->order.size();
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result
prefab_placement::freeze(runtime::application& application,
                         gneiss::engine::prepared_prefab_description prepared,
                         gneiss::engine::prefab_instance_description author,
                         std::vector<gneiss::engine::render_asset_ref> assets,
                         std::shared_ptr<const prefab_placement_snapshot>& output) noexcept try {
  auto* host = &application;
  const auto validation = host->is_owner_thread() && host->accepts_requests()
                              ? gneiss::engine::result::success
                              : gneiss::engine::result::invalid_state;
  if (validation != gneiss::engine::result::success) {
    return validation;
  }
  auto value = std::make_shared<prefab_placement_snapshot>();
  if (!prepared.resource)
    return gneiss::engine::result::invalid_argument;
  value->prefab = std::move(prepared.resource);
  value->author = std::move(author);
  value->order = std::move(prepared.parent_first);
  for (auto& asset : assets) {
    value->assets.emplace(gneiss::engine::asset_uri(asset), std::move(asset));
  }
  output = std::move(value);
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
gneiss::engine::result prefab_placement::apply(application& app, scene_instance_id scene,
                                               const prefab_placement_snapshot& snapshot,
                                               bool removing, bool replacing,
                                               scene_node_id& output) noexcept try {
  using namespace gneiss::engine;
  output = {};
  if (!app.is_owner_thread() || !app.accepts_requests() || !app.scenes())
    return result::invalid_state;
  if (removing)
    return replacing ? result::invalid_argument
                     : app.scenes()->remove_prefab_instance(scene, snapshot.author.instance_uuid);
  if (!snapshot.prefab)
    return result::invalid_argument;
  auto bindings = snapshot.bindings;
  if (bindings.empty()) {
    bindings.resize(snapshot.prefab.get()->objects.size());
    const auto find = [&](const std::string& uri) {
      const auto found = snapshot.assets.find(uri);
      return found == snapshot.assets.end() ? render_asset_ref{} : found->second;
    };
    for (std::size_t i = 0; i < bindings.size(); ++i) {
      const auto& object = snapshot.prefab.get()->objects[i];
      if (object.mesh_renderer)
        bindings[i] = {render_asset_loader::mesh_ref(find(object.mesh_renderer->mesh_uri)),
                       render_asset_loader::material_ref(find(object.mesh_renderer->material_uri))};
    }
  }
  return app.scenes()->instantiate_prefab_ready(scene, snapshot.author, snapshot.prefab, bindings,
                                                replacing, output);
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
}
