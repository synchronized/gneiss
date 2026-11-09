// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/scene_composition.hpp"
#include <algorithm>
namespace gneiss::engine {
result<composed_scene>
compose_scene(const scene_description& description,
              const std::map<std::string, const prefab_description*, std::less<>>& sources) {
  for (const auto& object : description.objects) {
    if (!object.prefab)
      continue;
    auto source = sources.find(object.prefab->uri);
    if (source == sources.end() || !source->second)
      return error::not_found;
  }
  const auto address_of = [&](object_address address) -> object_address {
    if (address.source || !address.object)
      return address;
    auto object =
        std::ranges::find(description.objects, address.object, &game_object_description::uuid);
    if (object != description.objects.end() && object->prefab)
      address.source = sources.at(object->prefab->uri)->root_uuid;
    return address;
  };
  composed_scene result;
  for (const auto& object : description.objects) {
    if (!object.prefab) {
      composed_object entry;
      entry.address = {object.uuid, {}};
      entry.parent = address_of(object.parent);
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
          root ? address_of(object.parent) : object_address{object.uuid, member.parent.object};
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
  // 完整来源已经提供，父地址必须能解析；在应用前拒绝丢失成员与循环。
  std::map<object_address, object_address> parents;
  for (const auto& object : result.objects)
    if (!object.address.object || !parents.emplace(object.address, object.parent).second)
      return error::invalid_argument;
  for (const auto& object : result.objects) {
    auto parent = object.parent;
    std::size_t depth = 0;
    while (parent.object) {
      auto found = parents.find(parent);
      if (found == parents.end())
        return error::not_found;
      if (++depth > parents.size())
        return error::invalid_argument;
      parent = found->second;
    }
    if (parent.source)
      return error::invalid_argument;
  }
  return result;
}
}
