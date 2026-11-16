// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/scene/prefab_replay.hpp>
#include <gneiss/engine/core/serialization/value.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <algorithm>
#include <map>
namespace gneiss {
namespace {
bool fatal(error status) { return status == error::out_of_memory || status == error::internal; }
scene_diagnostic location(error status, guid object, guid type, std::string path,
                          std::string message) {
  return {.error = status,
          .path = "/objects/" + object.string() + "/components/" + type.string() + path,
          .message = std::move(message)};
}
}
result<prefab_replay_result>
replay_prefab_reference(const prefab_description& source,
                        const prefab_reference_description& reference) {
  prefab_replay_result output{source, {}};
  using target_key = std::pair<guid, guid>;
  std::vector<target_key> structural;
  for (std::size_t i = 0; i < reference.component_overrides.size(); ++i) {
    const auto& change = reference.component_overrides[i];
    const target_key key{change.source_node_uuid, change.type_guid};
    scene_diagnostic diagnostic;
    error status = error::success;
    if (std::ranges::find(structural, key) != structural.end()) {
      status = error::invalid_argument;
      diagnostic = location(status, key.first, key.second, {}, "组件结构覆盖重复");
    } else {
      prefab_reference_description single;
      single.component_overrides.push_back(change);
      prefab_description candidate;
      status = prepare_prefab_reference(output.description, single, candidate, &diagnostic);
      if (status.ok()) {
        output.description = std::move(candidate);
        structural.push_back(key);
      }
    }
    if (fatal(status))
      return status;
    if (status.failed())
      output.conflicts.push_back({prefab_override_section::component, i, std::move(diagnostic)});
  }
  std::map<target_key, std::vector<std::size_t>> groups;
  for (std::size_t i = 0; i < reference.overrides.size(); ++i) {
    const auto& entry = reference.overrides[i];
    const auto& target = entry.key.target;
    auto status = validate_prefab_property_override(entry);
    auto object = std::ranges::find(output.description.objects, target.source_node_uuid,
                                    &game_object_description::uuid);
    if (status.ok() && object == output.description.objects.end())
      status = error::not_found;
    const target_key key{target.source_node_uuid,
                         component_description_type(target.target_type_guid)};
    if (status.ok() && std::ranges::find(structural, key) != structural.end())
      status = error::invalid_argument;
    if (status.ok() && target.target_type_guid != math::transform::type_guid &&
        (!target.component_uuid || object->components.identity(component_description_type(
                                       target.target_type_guid)) != target.component_uuid))
      status = error::not_found;
    if (status.ok() && object->components.find(target.target_type_guid)) {
      groups[{target.source_node_uuid, target.target_type_guid}].push_back(i);
      continue;
    }
    scene_diagnostic diagnostic;
    if (status.ok()) {
      // 迁移期 Transform/Camera 运行时属性身份仍使用原有适配器。
      prefab_reference_description single;
      single.overrides.push_back(entry);
      prefab_description candidate;
      status = prepare_prefab_reference(output.description, single, candidate, &diagnostic);
      if (status.ok())
        output.description = std::move(candidate);
    }
    if (fatal(status))
      return status;
    if (status.failed()) {
      if (diagnostic.path.empty())
        diagnostic = location(status, target.source_node_uuid, target.target_type_guid,
                              meta::property_path_text(entry.key.path), "覆盖目标不存在或不兼容");
      output.conflicts.push_back({prefab_override_section::property, i, std::move(diagnostic)});
    }
  }
  for (const auto& [key, indices] : groups) {
    auto object =
        std::ranges::find(output.description.objects, key.first, &game_object_description::uuid);
    const auto component = *object->components.find(key.second);
    auto view = component->view();
    if (!view)
      return view.error();
    auto captured = serialization::capture_value(*view);
    if (!captured)
      return captured.error().error;
    std::vector<value_edit> edits;
    for (const auto index : indices) {
      const auto& entry = reference.overrides[index];
      edits.push_back({entry.key.path, entry.operation});
    }
    auto replayed = replay_value_edits(*captured, edits);
    if (!replayed)
      return replayed.error();
    std::vector<bool> rejected(indices.size());
    for (const auto& conflict : replayed->conflicts) {
      rejected[conflict.operation] = true;
      output.conflicts.push_back({prefab_override_section::property, indices[conflict.operation],
                                  location(conflict.error, key.first, key.second,
                                           meta::property_path_text(edits[conflict.operation].path),
                                           "覆盖路径、元素或锚点已失效")});
    }
    auto changed = modify_component(component, [&](meta::instance candidate) {
      return serialization::apply_value(candidate, replayed->root).error;
    });
    if (changed)
      object->components.set(std::move(*changed));
    else {
      if (fatal(changed.error()))
        return changed.error();
      for (std::size_t i = 0; i < indices.size(); ++i)
        if (!rejected[i])
          output.conflicts.push_back({prefab_override_section::property, indices[i],
                                      location(changed.error(), key.first, key.second,
                                               meta::property_path_text(edits[i].path),
                                               "覆盖后的组件不满足类型或领域约束")});
    }
  }
  std::ranges::sort(output.conflicts, [](const auto& a, const auto& b) {
    return std::pair{a.section, a.index} < std::pair{b.section, b.index};
  });
  return output;
}
}
