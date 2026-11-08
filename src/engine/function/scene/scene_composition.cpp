// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/scene_composition.hpp"
#include <algorithm>
namespace gneiss::engine {
result<composed_scene>
compose_scene(const scene_description& description,
              const std::map<std::string, const prefab_description*, std::less<>>& sources) noexcept
    try {
  for (const auto& object : description.objects) {
    if (!object.prefab)
      continue;
    auto source = sources.find(object.prefab->uri);
    if (source == sources.end() || !source->second)
      return error::not_found;
  }
  const auto address_of = [&](guid uuid) -> object_address {
    auto object = std::ranges::find(description.objects, uuid, &game_object_description::uuid);
    return {uuid, object != description.objects.end() && object->prefab
                      ? sources.at(object->prefab->uri)->root_uuid
                      : guid{}};
  };
  composed_scene result;
  for (const auto& object : description.objects) {
    if (!object.prefab) {
      composed_object entry;
      entry.address = {object.uuid, {}};
      entry.parent = address_of(object.parent_uuid);
      entry.effective = object;
      result.objects.push_back(std::move(entry));
      continue;
    }
    const auto& source = *sources.at(object.prefab->uri);
    auto replayed = replay_prefab_reference(source, *object.prefab);
    if (!replayed)
      return replayed.error();
    if (!replayed->conflicts.empty())
      result.conflicts.emplace(object.uuid, std::move(replayed->conflicts));
    for (auto& member : replayed->description.objects) {
      const bool root = member.uuid == source.root_uuid;
      auto original =
          std::ranges::find(source.objects, member.uuid, &game_object_description::uuid);
      if (original == source.objects.end())
        return error::not_found;
      composed_object entry;
      entry.address = {object.uuid, member.uuid};
      entry.parent =
          root ? address_of(object.parent_uuid) : object_address{object.uuid, member.parent_uuid};
      entry.prefab_uri = object.prefab->uri;
      entry.source_transform = original->transform;
      entry.instance_root = root;
      for (const auto& change : object.prefab->overrides)
        if (change.key.target.source_node_uuid == member.uuid)
          entry.overrides.push_back(change.key);
      if (root) {
        member.name = object.name;
        member.transform = object.transform;
      }
      entry.effective = std::move(member);
      result.objects.push_back(std::move(entry));
    }
  }
  return result;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
}
