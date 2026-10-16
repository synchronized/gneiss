// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/application/prefab_placement.hpp"
#include "engine/function/application/application.hpp"
#include "engine/function/application/application_registry.hpp"
#include "engine/function/scene/structural_diff.hpp"
#include "engine/function/world/world_service.hpp"
#include <algorithm>

namespace gneiss::application_internal {
namespace {
gneiss_transform transform_of(const scene_internal::object_description& object) {
  gneiss_transform value{};
  std::ranges::copy(object.translation, value.translation);
  std::ranges::copy(object.rotation, value.rotation);
  std::ranges::copy(object.scale, value.scale);
  return value;
}
}
gneiss_result prefab_placement::capture_step(gneiss_application application, std::string_view uuid,
                                             gneiss_scene_instance scene,
                                             std::shared_ptr<prefab_placement_snapshot>& snapshot,
                                             bool& complete) noexcept try {
  complete = false;
  auto host = find_application(application);
  const auto validation = validate_application(host);
  if (validation != GNEISS_SUCCESS) {
    return validation;
  }
  if (!snapshot) {
    auto candidate = std::make_shared<prefab_placement_snapshot>();
    auto result =
        host->scenes()->capture_prefab_header(scene, uuid, candidate->author, candidate->prefab);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    scene_internal::structural_diff order;
    result = scene_internal::build_structural_diff({}, candidate->prefab.get()->objects, order);
    if (result != GNEISS_SUCCESS) {
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
    scene_internal::scene_mesh_binding binding;
    const auto result =
        host->scenes()->capture_prefab_binding(scene, uuid, snapshot->bindings.size(), binding);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    snapshot->bindings.push_back(std::move(binding));
  }
  complete = snapshot->bindings.size() == snapshot->order.size();
  return GNEISS_SUCCESS;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

gneiss_result
prefab_placement::freeze(gneiss_application application,
                         scene_internal::prepared_prefab_description prepared,
                         scene_internal::prefab_instance_description author,
                         std::vector<render_internal::render_asset_lease> assets,
                         std::shared_ptr<const prefab_placement_snapshot>& output) noexcept try {
  auto host = find_application(application);
  const auto validation = validate_application(host);
  if (validation != GNEISS_SUCCESS) {
    return validation;
  }
  auto value = std::make_shared<prefab_placement_snapshot>();
  auto& loader = host->prefab_loader();
  const auto result =
      loader.install_prepared(author.prefab_uri, std::move(prepared.description), value->prefab);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  value->author = std::move(author);
  value->order = std::move(prepared.parent_first);
  for (auto& asset : assets) {
    value->assets.emplace(asset.uri(), std::move(asset));
  }
  output = std::move(value);
  return GNEISS_SUCCESS;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}
prefab_placement::prefab_placement(gneiss_application application,
                                   std::shared_ptr<const prefab_placement_snapshot> snapshot,
                                   gneiss_scene_instance scene, bool removing)
    : application_(application), scene_(scene), snapshot_(std::move(snapshot)),
      author_(snapshot_->author), removing_(removing) {}
prefab_placement::~prefab_placement() = default;
gneiss_result prefab_placement::begin(gneiss_application application, gneiss_scene_instance scene,
                                      std::shared_ptr<const prefab_placement_snapshot> snapshot,
                                      bool removing, std::unique_ptr<prefab_placement>& output,
                                      bool replacing) noexcept try {
  output.reset();
  auto host = find_application(application);
  const auto validation = validate_application(host);
  if (validation != GNEISS_SUCCESS || !snapshot) {
    return validation == GNEISS_SUCCESS ? GNEISS_ERROR_INVALID_ARGUMENT : validation;
  }
  auto value = std::unique_ptr<prefab_placement>(
      new prefab_placement(application, std::move(snapshot), scene, removing));
  if (removing && replacing) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  value->replacing_ = replacing;
  auto result = GNEISS_SUCCESS;
  if (!removing) {
    result = host->scenes()->begin_prefab_candidate(scene, value->author_, value->snapshot_->prefab,
                                                    *value->snapshot_->prefab.get(),
                                                    value->runtime_, replacing);
    if (value->runtime_) {
      value->root_ = value->runtime_->root();
    }
  }
  output = std::move(value);
  return result;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}
gneiss_result prefab_placement::create_next_node() {
  const auto index = snapshot_->order[cursor_];
  const auto& source = snapshot_->prefab.get()->objects.at(index);
  const auto find = [&](const std::string& uri) {
    const auto found = snapshot_->assets.find(uri);
    return found == snapshot_->assets.end() ? render_internal::render_asset_lease{} : found->second;
  };
  scene_internal::scene_mesh_binding binding;
  if (!snapshot_->bindings.empty()) {
    binding = snapshot_->bindings.at(index);
  } else if (source.mesh_renderer) {
    binding = {
        .mesh = find(source.mesh_renderer->mesh_uri),
        .material = find(source.mesh_renderer->material_uri),
    };
  }
  const auto result = runtime_->create_staged_node(
      index, source.parent_uuid ? nodes_.at(*source.parent_uuid) : runtime_->root(),
      render_internal::render_asset_loader::mesh_lease(binding.mesh),
      render_internal::render_asset_loader::material_lease(binding.material));
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  scene_internal::prefab_runtime_instance::node_info info;
  const auto queried = runtime_->get_node_info(index, info);
  if (queried != GNEISS_SUCCESS) {
    return queried;
  }
  nodes_.emplace(source.uuid, info.node);
  ++cursor_;

  return GNEISS_SUCCESS;
}
gneiss_result prefab_placement::advance(bool& ready) noexcept try {
  ready = false;
  auto host = find_application(application_);
  const auto validation = validate_application(host);
  if (validation != GNEISS_SUCCESS || committed_) {
    return validation == GNEISS_SUCCESS ? GNEISS_ERROR_INVALID_STATE : validation;
  }
  for (unsigned step = 0; step < 64U && !ready_; ++step) {
    if (removing_) {
      const auto result =
          host->scenes()->prepare_prefab_removal(scene_, author_.instance_uuid, ready_);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    } else if (cursor_ < snapshot_->order.size()) {
      const auto created = create_next_node();
      if (created != GNEISS_SUCCESS) {
        return created;
      }
    } else if (replacing_ && !retirement_ready_) {
      const auto result =
          host->scenes()->prepare_prefab_removal(scene_, author_.instance_uuid, retirement_ready_);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    } else {
      ready_ = true;
    }
  }
  ready = ready_;
  return GNEISS_SUCCESS;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}
std::size_t prefab_placement::node_count() const noexcept { return snapshot_->order.size() + 1U; }
gneiss_scene_node_id prefab_placement::root() const noexcept { return root_; }
gneiss_result prefab_placement::preview(std::size_t index,
                                        scene_internal::prefab_node_view& output) const noexcept {
  output = {};
  if (!ready_ || removing_ || !runtime_ || index >= node_count()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  output.instance_uuid = author_.instance_uuid;
  output.prefab_uri = author_.prefab_uri;
  if (index == 0U) {
    output.flags = GNEISS_SCENE_PREFAB_NODE_INSTANCE_ROOT;
    output.node = runtime_->root();
    output.entity = runtime_->root_entity();
    output.name = author_.name;
    std::ranges::copy(author_.translation, output.local_transform.translation);
    std::ranges::copy(author_.rotation, output.local_transform.rotation);
    std::ranges::copy(author_.scale, output.local_transform.scale);
    return GNEISS_SUCCESS;
  }
  scene_internal::prefab_runtime_instance::node_info info;
  const auto result = runtime_->get_node_info(index - 1U, info);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  output.flags = GNEISS_SCENE_PREFAB_NODE_SOURCE_READ_ONLY;
  output.node = info.node;
  output.entity = info.entity;
  output.parent = info.parent;
  output.source_node_uuid = info.address->source_node_uuid;
  output.name = info.name;
  output.local_transform = transform_of(snapshot_->prefab.get()->objects[index - 1U]);
  output.source_local_transform = output.local_transform;
  output.local_transform = transform_of(runtime_->candidate_description().objects[index - 1U]);
  const auto transform_type = world_internal::transform_type_id();
  for (const auto& value : author_.overrides) {
    if (value.key.node.source_node_uuid != output.source_node_uuid ||
        !std::ranges::equal(value.key.type_id.bytes, transform_type.bytes)) {
      continue;
    }
    switch (value.key.field_id) {
    case GNEISS_TRANSFORM_FIELD_TRANSLATION:
      output.flags |= GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN;
      break;
    case GNEISS_TRANSFORM_FIELD_ROTATION:
      output.flags |= GNEISS_SCENE_PREFAB_NODE_ROTATION_OVERRIDDEN;
      break;
    case GNEISS_TRANSFORM_FIELD_SCALE:
      output.flags |= GNEISS_SCENE_PREFAB_NODE_SCALE_OVERRIDDEN;
      break;
    default:
      break;
    }
  }
  return GNEISS_SUCCESS;
}
gneiss_result prefab_placement::commit() noexcept {
  auto host = find_application(application_);
  const auto validation = validate_application(host);
  if (validation != GNEISS_SUCCESS || !ready_ || committed_ || replacing_) {
    return validation == GNEISS_SUCCESS ? GNEISS_ERROR_INVALID_STATE : validation;
  }
  const auto result =
      removing_ ? host->scenes()->commit_prefab_removal(scene_, author_.instance_uuid, runtime_)
                : host->scenes()->commit_prefab_candidate(scene_, author_, runtime_);
  committed_ = result == GNEISS_SUCCESS;
  return result;
}
gneiss_result
prefab_placement::commit_replacements(std::span<prefab_placement* const> entries) noexcept try {
  if (entries.empty() || entries.front() == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto application = entries.front()->application_;
  const auto scene = entries.front()->scene_;
  auto host = find_application(application);
  const auto validation = validate_application(host);
  if (validation != GNEISS_SUCCESS) {
    return validation;
  }
  std::vector<scene_internal::scene_instance_service::prefab_replacement> replacements;
  replacements.reserve(entries.size());
  for (auto* entry : entries) {
    if (entry == nullptr || entry->application_ != application || entry->scene_ != scene ||
        !entry->replacing_ || !entry->ready_ || entry->committed_) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    replacements.push_back({.author = &entry->author_, .runtime = &entry->runtime_});
  }
  const auto result = host->scenes()->commit_prefab_replacements(scene, replacements);
  if (result == GNEISS_SUCCESS) {
    for (auto* entry : entries) {
      entry->committed_ = true;
    }
  }
  return result;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

bool prefab_placement::cleanup_step(std::size_t budget) noexcept {
  if (!runtime_) {
    return true;
  }
  if (!runtime_->cancel_staged_step(budget)) {
    return false;
  }
  runtime_.reset();
  return true;
}
}
