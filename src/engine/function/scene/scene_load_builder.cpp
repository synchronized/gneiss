// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_load_builder.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/core/diagnostics/profiling.hpp"

#include <algorithm>

namespace gneiss::scene_internal {

scene_load_builder::scene_load_builder(scene_instance_service& service,
                                       prepared_scene_description prepared)
    : service_(service),
      prepared_(diagnostics::measure(diagnostics::loop_stage::scene_description_move,
                                     [&] {
                                       GNEISS_PROFILE_SCOPE("scene.description.move");
                                       return std::move(prepared);
                                     })),
      instance_(diagnostics::measure(diagnostics::loop_stage::scene_instance_allocate, [&] {
        GNEISS_PROFILE_SCOPE("scene.instance.allocate");
        return std::make_unique<scene_instance>(service.world_, service.loader_,
                                                service.prefab_loader_, service.registry_);
      })) {
  diagnostics::measure(diagnostics::loop_stage::scene_instance_initialize, [&] {
    GNEISS_PROFILE_SCOPE("scene.instance.initialize");
    instance_->initialize_staged(std::move(prepared_.description));
  });
  diagnostics::measure(diagnostics::loop_stage::scene_node_index_reserve,
                       [&] { nodes_.reserve(instance_->description.objects.size()); });
}

gneiss_result scene_load_builder::step_prefab() {
  const auto& source = instance_->description.prefab_instances[prefab_cursor_];
  const auto& prepared = prepared_.prefabs.at(source.prefab_uri);
  if (!prefab_) {
    const auto lease = prefabs_.at(source.prefab_uri);
    prefab_ = std::make_unique<prefab_runtime_instance>(service_.world_, service_.loader_, lease,
                                                        source.instance_uuid);
    gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
    std::ranges::copy(source.translation, transform.translation);
    std::ranges::copy(source.rotation, transform.rotation);
    std::ranges::copy(source.scale, transform.scale);
    prefab_node_cursor_ = override_cursor_ = 0U;
    prefab_nodes_.clear();
    prefab_nodes_.reserve(lease.get()->objects.size());
    const auto result = prefab_->begin_staged(
        source.parent_uuid ? nodes_.at(*source.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID, transform);
    if (result == GNEISS_SUCCESS) {
      ++completed_nodes_;
    }
    return result;
  }
  if (prefab_node_cursor_ < prepared.parent_first.size()) {
    const auto index = prepared.parent_first[prefab_node_cursor_];
    const auto& node = prefab_->prefab_lease().get()->objects[index];
    const auto result = prefab_->create_staged_node(
        index, node.parent_uuid ? prefab_nodes_.at(*node.parent_uuid) : prefab_->root());
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    prefab_runtime_instance::node_info info;
    const auto query = prefab_->get_node_info(index, info);
    if (query != GNEISS_SUCCESS) {
      return query;
    }
    prefab_nodes_.emplace(node.uuid, info.node);
    ++prefab_node_cursor_;
    ++completed_nodes_;
    return GNEISS_SUCCESS;
  }
  if (override_cursor_ < source.overrides.size()) {
    return prefab_->apply_staged_override(service_.registry_, source.overrides[override_cursor_++]);
  }
  instance_->prefab_instances.push_back(std::move(prefab_));
  ++prefab_cursor_;
  return GNEISS_SUCCESS;
}

gneiss_result scene_load_builder::step() {
  GNEISS_PROFILE_SCOPE("scene.build.step");
  if (prefabs_.size() < prepared_.prefabs.size()) {
    // map 的稳定键顺序与安装顺序一致；每次最多安装一份已经解析的描述。
    auto source = prepared_.prefabs.begin();
    std::advance(source, static_cast<std::ptrdiff_t>(prefabs_.size()));
    prefab_asset_lease lease;
    const auto result = service_.prefab_loader_.install_prepared(
        source->first, std::move(source->second.description), lease);
    if (result == GNEISS_SUCCESS) {
      prefabs_.emplace(source->first, std::move(lease));
    }
    return result;
  }
  if (object_cursor_ < prepared_.parent_first.size()) {
    const auto index = prepared_.parent_first[object_cursor_];
    const auto& source = instance_->description.objects[index];
    const auto result = instance_->create_staged_node(
        index, source.parent_uuid ? nodes_.at(*source.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID);
    if (result == GNEISS_SUCCESS) {
      nodes_.emplace(source.uuid, instance_->objects[index].node);
      ++object_cursor_;
      ++completed_nodes_;
    }
    return result;
  }
  if (prefab_cursor_ < instance_->description.prefab_instances.size()) {
    return step_prefab();
  }
  return service_.instances_.create(core::resource_type::scene_instance, std::move(instance_),
                                    &handle_);
}

gneiss_result scene_load_builder::advance(bool& complete, std::size_t maximum_steps,
                                          std::chrono::nanoseconds maximum_time) noexcept {
  complete = handle_ != GNEISS_NULL_SCENE_INSTANCE;
  if (complete || failure_ != GNEISS_SUCCESS) {
    return failure_;
  }
  if (maximum_steps == 0U || maximum_time <= std::chrono::nanoseconds::zero()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < maximum_steps; ++index) {
      failure_ = step();
      complete = handle_ != GNEISS_NULL_SCENE_INSTANCE;
      if (failure_ != GNEISS_SUCCESS || complete ||
          std::chrono::steady_clock::now() - start >= maximum_time) {
        break;
      }
    }
  } catch (const std::bad_alloc&) {
    failure_ = GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    failure_ = GNEISS_ERROR_INTERNAL;
  }
  return failure_;
}

} // namespace gneiss::scene_internal
