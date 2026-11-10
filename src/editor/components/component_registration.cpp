// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "components/component_registration.hpp"
#include <algorithm>
#include <mutex>
#include <numbers>
namespace gneiss::editor {
using namespace engine;
namespace {
struct editor_registry {
  std::mutex mutex;
  bool frozen{};
  std::vector<component_editor_adapter> values{
      {.type = camera_description::type_guid,
       .create_default = []() -> result<component_description> {
         return make_component_description(
             camera_description{{std::numbers::pi_v<float> / 3, 0.1F, 1000.0F}});
       },
       .removable = true,
       .prepare = [](game_object target,
                     const component_description& source) -> result<component_description> {
         if (!source || !source->get<camera_description>())
           return error::invalid_argument;
         auto current = capture_components(*target.owner(), entity_id{target.entity().get()});
         if (!current)
           return current.error();
         auto camera = current->get<camera_description>();
         if (!camera)
           return error::not_found;
         auto next = *source->get<camera_description>();
         next.is_primary = camera->is_primary;
         return make_component_description(std::move(next));
       }},
      {.type = mesh_renderer_description::type_guid,
       .removable = true,
       .route = component_editor_route::mesh_assets}};
};
editor_registry& registry() {
  static editor_registry value;
  return value;
}
const component_editor_adapter* find(guid type) {
  auto& state = registry();
  std::lock_guard lock(state.mutex);
  state.frozen = true;
  auto found = std::ranges::find(state.values, type, &component_editor_adapter::type);
  return found == state.values.end() ? nullptr : &*found;
}
bool registered(guid type) {
  auto types = component_types();
  return std::ranges::find(types, type) != types.end();
}
result<component_description> validated(result<component_description> candidate, guid type) {
  if (!candidate)
    return candidate.error();
  if (!*candidate || (*candidate)->guid() != type)
    return error::invalid_argument;
  component_descriptions values;
  auto status = values.replace({*candidate});
  if (status.ok())
    status = validate_components(values);
  return status.ok() ? std::move(candidate) : result<component_description>{status};
}
}
error register_component_editor(component_editor_adapter adapter) {
  if (!adapter.type)
    return error::invalid_argument;
  auto& state = registry();
  std::lock_guard lock(state.mutex);
  if (state.frozen)
    return error::invalid_state;
  if (std::ranges::find(state.values, adapter.type, &component_editor_adapter::type) !=
      state.values.end())
    return error::invalid_argument;
  state.values.push_back(std::move(adapter));
  return error::success;
}
result<component_editor_info> component_editor(guid type) {
  if (!registered(type))
    return error::not_found;
  auto policy = find(type);
  return policy ? component_editor_info{type, bool(policy->create_default), policy->removable,
                                        policy->route}
                : component_editor_info{type};
}
result<std::vector<component_editor_info>> component_editor_types() {
  std::vector<component_editor_info> output;
  for (auto type : component_types()) {
    auto info = component_editor(type);
    if (!info)
      return info.error();
    output.push_back(*info);
  }
  return output;
}
result<component_description> default_component_description(guid type) {
  if (!registered(type))
    return error::not_found;
  auto policy = find(type);
  return policy && policy->create_default ? validated(policy->create_default(), type)
                                          : result<component_description>{error::unsupported};
}
result<component_description> prepare_component_editor_value(world& owner, entity_id node,
                                                             const component_description& source) {
  if (!source)
    return error::invalid_argument;
  if (!registered(source->guid()))
    return error::not_found;
  auto target = owner.object(entity_id{node.get()});
  if (!target)
    return target.error();
  auto policy = find(source->guid());
  return validated(policy && policy->prepare ? policy->prepare(*target, source)
                                             : result<component_description>{source},
                   source->guid());
}
}
