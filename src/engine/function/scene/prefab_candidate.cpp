// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/prefab_candidate.hpp"
#include "engine/core/reflection/component_identity.hpp"
#include "engine/core/result.hpp"
#include "engine/function/scene/prefab_runtime_instance.hpp"
#include "engine/function/world/world_service.hpp"
#include <algorithm>
namespace gneiss::engine {
namespace {
gneiss::engine::transform transform_of(const gneiss::engine::object_description& source) noexcept {
  return {source.translation, source.rotation, source.scale};
}
}
prefab_candidate::prefab_candidate() noexcept = default;
prefab_candidate::~prefab_candidate() noexcept = default;
prefab_candidate::prefab_candidate(prefab_candidate&&) noexcept = default;
prefab_candidate& prefab_candidate::operator=(prefab_candidate&&) noexcept = default;
bool prefab_candidate::is_valid() const noexcept { return runtime_ != nullptr; }
scene_node_id prefab_candidate::root() const noexcept {
  return runtime_ ? scene_node_id{runtime_->root()} : scene_node_id{};
}
gneiss::engine::result prefab_candidate::create_node(std::size_t index, scene_node_id parent,
                                                     gneiss::engine::render_asset_ref mesh,
                                                     gneiss::engine::render_asset_ref material,
                                                     scene_node_id& output) {
  if (!runtime_ || !runtime_->native_world_.is_owner_thread() || runtime_->published_ ||
      runtime_->cancelling_)
    return gneiss::engine::result::invalid_state;
  const auto result =
      runtime_->create_staged_node(index, parent.get(), std::move(mesh), std::move(material));
  if (result != gneiss::engine::result::success)
    return result;
  prefab_runtime_instance::node_info info;
  const auto queried = runtime_->get_node_info(index, info);
  if (queried == gneiss::engine::result::success)
    output = scene_node_id{info.node};
  return queried;
}
bool prefab_candidate::cleanup_step(std::size_t budget) noexcept {
  if (!runtime_)
    return true;
  if (!runtime_->native_world_.is_owner_thread())
    return false;
  if (!runtime_->cancel_staged_step(budget))
    return false;
  runtime_.reset();
  return true;
}
gneiss::engine::result prefab_candidate::preview(std::size_t index,
                                                 prefab_node_view& output) const noexcept {
  output = {};
  if (!runtime_ || !runtime_->native_world_.is_owner_thread() || runtime_->published_ ||
      runtime_->cancelling_ || index > runtime_->node_count())
    return gneiss::engine::result::invalid_state;
  output.instance_uuid = author_.instance_uuid;
  output.prefab_uri = author_.prefab_uri;
  if (index == 0U) {
    output.flags = scene_prefab_flags::instance_root;
    output.node = scene_node_id{runtime_->root()};
    output.entity = entity_id{runtime_->root_entity()};
    output.name = author_.name;
    std::ranges::copy(author_.translation, output.local_transform.translation.begin());
    std::ranges::copy(author_.rotation, output.local_transform.rotation.begin());
    std::ranges::copy(author_.scale, output.local_transform.scale.begin());
    return gneiss::engine::result::success;
  }
  gneiss::engine::prefab_runtime_instance::node_info info;
  const auto result = runtime_->get_node_info(index - 1U, info);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  output.flags = scene_prefab_flags::source_read_only;
  output.node = scene_node_id{info.node};
  output.entity = entity_id{info.entity};
  output.parent = scene_node_id{info.parent};
  output.source_node_uuid = info.address->source_node_uuid;
  output.name = info.name;
  output.local_transform = transform_of(runtime_->prefab_lease().get()->objects[index - 1U]);
  output.source_local_transform = output.local_transform;
  output.local_transform = transform_of(runtime_->candidate_description().objects[index - 1U]);
  const auto transform_type = gneiss::engine::transform_component_type;
  for (const auto& value : author_.overrides) {
    if (value.key.node.source_node_uuid != output.source_node_uuid ||
        value.key.type_id != transform_type) {
      continue;
    }
    switch (value.key.field_id.get()) {
    case GNEISS_TRANSFORM_FIELD_TRANSLATION:
      output.flags |= scene_prefab_flags::translation_overridden;
      break;
    case GNEISS_TRANSFORM_FIELD_ROTATION:
      output.flags |= scene_prefab_flags::rotation_overridden;
      break;
    case GNEISS_TRANSFORM_FIELD_SCALE:
      output.flags |= scene_prefab_flags::scale_overridden;
      break;
    default:
      break;
    }
  }
  return gneiss::engine::result::success;
}
}
