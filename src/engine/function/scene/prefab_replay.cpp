// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/prefab_replay.hpp"
#include "engine/core/property_edit_policy.hpp"
#include "engine/core/serialization/value.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/world/component_identity.hpp"
#include <algorithm>
#include <map>
namespace gneiss::engine {
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
                        const prefab_reference_description& reference) noexcept try {
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
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
namespace {
error collect_component_overrides(meta::const_instance source, meta::const_instance candidate,
                                  prefab_property_override_key key,
                                  std::vector<prefab_property_override>& output,
                                  unsigned depth = 0) {
  if (depth > 64 || source.type_id() != candidate.type_id())
    return error::invalid_argument;
  if (source.kind() == meta::type_kind::object) {
    auto properties = source.properties();
    if (!source.type().registered())
      return error::not_found;
    for (const auto& property : properties) {
      // 派生运行状态仍可保存，但不因普通组件编辑生成实例属性覆盖。
      if (!meta::has_flags(property.capabilities(), meta::property_capabilities::writable) ||
          gneiss::engine::is_edit_read_only(property))
        continue;
      auto left = property.get(source), right = property.get(candidate);
      if (!left)
        return left.error();
      if (!right)
        return right.error();
      auto next = key;
      next.path.emplace_back(property.id());
      auto status = collect_component_overrides(*left, *right, std::move(next), output, depth + 1);
      if (status.failed())
        return status;
    }
    return error::success;
  }
  auto before = serialization::capture_value(source),
       after = serialization::capture_value(candidate);
  if (!before)
    return before.error().error;
  if (!after)
    return after.error().error;
  auto edits = diff_values(*before, *after);
  if (!edits)
    return edits.error();
  for (auto& edit : *edits) {
    auto target = key;
    target.path.insert(target.path.end(), edit.path.begin(), edit.path.end());
    output.push_back({std::move(target), std::move(edit.operation)});
  }
  return error::success;
}
result<prefab_edit_candidate> prepare_property_edit(const prefab_description& source,
                                                    const prefab_reference_description& reference,
                                                    guid source_node, guid target_type,
                                                    meta::const_instance original,
                                                    meta::const_instance current,
                                                    meta::const_instance replacement) {
  auto prior = replay_prefab_reference(source, reference);
  if (!prior)
    return prior.error();
  std::vector<prefab_property_override> touched;
  auto status = collect_component_overrides(current, replacement, {}, touched);
  if (status.failed())
    return status;
  auto overlaps = [](const auto& left, const auto& right) {
    const auto length = std::min(left.size(), right.size());
    return std::equal(left.begin(), left.begin() + length, right.begin());
  };
  auto object = std::ranges::find(source.objects, source_node, &game_object_description::uuid);
  const auto identity =
      target_type == math::transform::type_guid ? guid{} : object->components.identity(target_type);
  prefab_edit_candidate output{reference, {}};
  auto& pending = output.reference.overrides;
  pending.clear();
  for (std::size_t i = 0; i < reference.overrides.size(); ++i) {
    const auto& old = reference.overrides[i];
    if (old.key.target.source_node_uuid != source_node ||
        component_description_type(old.key.target.target_type_guid) != target_type ||
        old.key.target.component_uuid != identity) {
      pending.push_back(old);
      continue;
    }
    const bool conflict = std::ranges::any_of(prior->conflicts, [&](const auto& item) {
      return item.section == prefab_override_section::property && item.index == i;
    });
    const bool changed = std::ranges::any_of(
        touched, [&](const auto& item) { return overlaps(old.key.path, item.key.path); });
    if (conflict && !changed)
      pending.push_back(old);
  }
  status = collect_component_overrides(
      original, replacement, {.target = {source_node, target_type, identity}, .path = {}}, pending);
  if (status.failed())
    return status;
  auto replayed = replay_prefab_reference(source, output.reference);
  if (!replayed)
    return replayed.error();
  output.effective = std::move(replayed->description);
  output.conflicts = std::move(replayed->conflicts);
  return output;
}
}

result<prefab_edit_candidate>
prepare_prefab_component_edit(const prefab_description& source,
                              const prefab_reference_description& reference, guid source_node,
                              const component_description& current,
                              const component_description& replacement) noexcept try {
  if (!source_node || !current || !replacement || current->guid() != replacement->guid())
    return error::invalid_argument;
  auto object = std::ranges::find(source.objects, source_node, &game_object_description::uuid);
  if (object == source.objects.end())
    return error::not_found;
  const auto* original = object->components.find(replacement->guid());
  auto structural = std::ranges::find_if(reference.component_overrides, [&](const auto& entry) {
    return entry.source_node_uuid == source_node && entry.type_guid == replacement->guid();
  });
  // 编辑已有本地组件不改变其增删语义；移除来源组件不能被属性编辑隐式恢复。
  if (structural != reference.component_overrides.end() && (!structural->added || original))
    return error::invalid_state;
  if (!original && structural == reference.component_overrides.end())
    return error::not_found;
  if (structural != reference.component_overrides.end() &&
      (!*structural->added || (*structural->added)->guid() != replacement->guid()))
    return error::invalid_argument;
  auto valid = validate_components(component_descriptions{replacement});
  if (valid.failed())
    return valid;
  if (!original) {
    prefab_edit_candidate output{reference, {}};
    const auto index = static_cast<std::size_t>(structural - reference.component_overrides.begin());
    auto identified = with_component_identity(replacement, structural->component_uuid);
    if (!identified)
      return identified.error();
    output.reference.component_overrides[index].added = std::move(*identified);
    auto replayed = replay_prefab_reference(source, output.reference);
    if (!replayed)
      return replayed.error();
    for (const auto& conflict : replayed->conflicts) {
      const auto& target =
          conflict.section == prefab_override_section::component
              ? prefab_property_target{output.reference.component_overrides[conflict.index]
                                           .source_node_uuid,
                                       output.reference.component_overrides[conflict.index]
                                           .type_guid}
              : output.reference.overrides[conflict.index].key.target;
      if (target.source_node_uuid == source_node &&
          component_description_type(target.target_type_guid) == replacement->guid())
        return conflict.diagnostic.error;
    }
    output.effective = std::move(replayed->description);
    output.conflicts = std::move(replayed->conflicts);
    return output;
  }
  auto before = (*original)->view(), after = replacement->view(), current_view = current->view();
  if (!before)
    return before.error();
  if (!after)
    return after.error();
  if (!current_view)
    return current_view.error();
  return prepare_property_edit(source, reference, source_node, replacement->guid(), *before,
                               *current_view, *after);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<prefab_edit_candidate> prepare_prefab_component_override(
    const prefab_description& source, const prefab_reference_description& reference,
    guid source_node, guid type, const component_description& added, bool removed,
    std::span<const prefab_property_override> properties, guid component_uuid) noexcept try {
  if (!source_node || !type || (added && removed) || ((added || removed) && !properties.empty()) ||
      (added && added->guid() != type))
    return error::invalid_argument;
  if (std::ranges::find(source.objects, source_node, &game_object_description::uuid) ==
      source.objects.end())
    return error::not_found;
  prefab_edit_candidate output{reference, {}};
  auto& pending = output.reference;
  std::erase_if(pending.component_overrides, [&](const auto& value) {
    return value.source_node_uuid == source_node && value.type_guid == type;
  });
  std::erase_if(pending.overrides, [&](const auto& value) {
    return value.key.target.source_node_uuid == source_node &&
           component_description_type(value.key.target.target_type_guid) == type;
  });
  if (added || removed) {
    auto object = std::ranges::find(source.objects, source_node, &game_object_description::uuid);
    guid identity = component_uuid ? component_uuid
                    : removed      ? object->components.identity(type)
                                   : guid::generate();
    auto prior = std::ranges::find_if(reference.component_overrides, [&](const auto& item) {
      return item.source_node_uuid == source_node && item.type_guid == type && item.added;
    });
    if (added && !component_uuid && prior != reference.component_overrides.end())
      identity = prior->component_uuid;
    std::optional<component_description> identified;
    if (added) {
      auto copied = with_component_identity(added, identity);
      if (!copied)
        return copied.error();
      identified = std::move(*copied);
    }
    pending.component_overrides.push_back({source_node, type, std::move(identified), identity});
  }
  for (const auto& value : properties) {
    if (value.key.target.source_node_uuid != source_node ||
        component_description_type(value.key.target.target_type_guid) != type)
      return error::invalid_argument;
    pending.overrides.push_back(value);
  }
  auto replayed = replay_prefab_reference(source, pending);
  if (!replayed)
    return replayed.error();
  for (const auto& conflict : replayed->conflicts) {
    const auto edited =
        conflict.section == prefab_override_section::component
            ? pending.component_overrides[conflict.index].source_node_uuid == source_node &&
                  pending.component_overrides[conflict.index].type_guid == type
            : pending.overrides[conflict.index].key.target.source_node_uuid == source_node &&
                  component_description_type(
                      pending.overrides[conflict.index].key.target.target_type_guid) == type;
    if (edited)
      return conflict.diagnostic.error;
  }
  output.effective = std::move(replayed->description);
  output.conflicts = std::move(replayed->conflicts);
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<prefab_edit_candidate>
prepare_prefab_transform_edit(const prefab_description& source,
                              const prefab_reference_description& reference, guid source_node,
                              const math::transform& current,
                              const math::transform& replacement) noexcept try {
  if (!source_node || source_node == source.root_uuid || !math::is_valid_transform(replacement))
    return error::invalid_argument;
  auto object = std::ranges::find(source.objects, source_node, &game_object_description::uuid);
  if (object == source.objects.end())
    return error::not_found;
  auto before = meta::view(std::as_const(object->transform));
  auto now = meta::view(std::as_const(current));
  auto after = meta::view(std::as_const(replacement));
  if (!before)
    return before.error();
  if (!now)
    return now.error();
  if (!after)
    return after.error();
  return prepare_property_edit(source, reference, source_node, math::transform::type_guid, *before,
                               *now, *after);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
}
