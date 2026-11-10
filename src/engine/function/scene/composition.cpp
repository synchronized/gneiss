// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/composition.hpp"
#include "engine/function/scene/component_registration.hpp"
#include "engine/resource/scene/object_hierarchy.hpp"
#include <algorithm>
namespace gneiss::engine {
result<composed_scene>
compose_scene(const scene_description& description,
              const std::map<std::string, const prefab_description*, std::less<>>& sources) {
  auto hierarchy = inspect_object_hierarchy(description.objects, std::nullopt, 1024, {});
  if (!hierarchy)
    return hierarchy.error().error;
  for (const auto& object : description.objects) {
    if (!object.prefab)
      continue;
    auto source = sources.find(object.prefab->uri);
    if (source == sources.end() || !source->second)
      return error::not_found;
    auto prefab_hierarchy =
        inspect_object_hierarchy(source->second->objects, source->second->root_uuid, 1024, {});
    if (!prefab_hierarchy)
      return prefab_hierarchy.error().error;
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
      composed_object entry;
      entry.address = {object.uuid, member.uuid};
      entry.parent =
          root ? address_of(object.parent) : object_address{object.uuid, member.parent.object};
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
result<std::vector<render_resource_source>> scene_dependencies(const composed_scene& scene) {
  std::map<std::string, render_resource_kind, std::less<>> unique;
  for (const auto& object : scene.objects) {
    auto dependencies = component_dependencies(object.effective.components);
    if (!dependencies)
      return dependencies.error();
    for (const auto& dependency : *dependencies) {
      auto [found, inserted] = unique.emplace(dependency.uri, dependency.type);
      if (!inserted && found->second != dependency.type)
        return error::invalid_argument;
    }
  }
  std::vector<render_resource_source> result;
  result.reserve(unique.size());
  for (const auto& [uri, kind] : unique)
    result.push_back({uri, kind});
  return result;
}
}
