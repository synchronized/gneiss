// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "panels/property_inspector_model.hpp"
#include "engine/core/result.hpp"
#include "engine/function/world/world_reflection.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <new>
#include <utility>

namespace gneiss::editor {
gneiss::engine::result property_inspector_model::initialize() noexcept {
  return gneiss::engine::create_property_registry(registry_);
}

void property_inspector_model::clear() noexcept {
  components_.clear();
  target_ = {};
}

gneiss::engine::result property_inspector_model::refresh(gneiss_world world_handle,
                                                         entity_id entity) noexcept {
  if (!registry_ || world_handle == GNEISS_NULL_WORLD || !entity.is_valid()) {
    clear();
    return gneiss::engine::result::invalid_argument;
  }
  try {
    const property_target target{.context = world_handle, .object = entity.get()};
    std::vector<inspector_component> pending;
    const std::array reflected_component_types{
        gneiss::engine::transform_property_type(),
        gneiss::engine::camera_property_type(),
    };
    for (const auto type_id : reflected_component_types) {
      type_info type{};
      auto operation = gneiss::engine::describe_type(registry_, type_id, type);
      if (operation != gneiss::engine::result::success) {
        clear();
        return operation;
      }
      inspector_component component{
          .type_id = type.id,
          .name = std::string(type.name),
          .properties = {},
      };
      bool component_missing = false;
      component.properties.reserve(type.fields.size());
      for (std::uint32_t index = 0; index < type.fields.size(); ++index) {
        const auto& field = type.fields[index];
        gneiss::engine::property_value value;
        operation = gneiss::engine::read_property(registry_, type.id, field.id, target, value);
        if (operation == gneiss::engine::result::not_found) {
          component_missing = true;
          break;
        }
        if (operation != gneiss::engine::result::success) {
          clear();
          return operation;
        }
        component.properties.push_back({
            .id = field.id,
            .name = std::string(field.name),
            .kind = field.kind,
            .capabilities = field.capabilities,
            .value = std::move(value),
        });
      }
      if (!component_missing) {
        pending.push_back(std::move(component));
      }
    }
    components_ = std::move(pending);
    target_ = target;
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result
property_inspector_model::set_value(gneiss::engine::type_guid type_id, field_id field_id,
                                    const gneiss::engine::property_value& value) noexcept {
  if (!registry_ || target_.context == GNEISS_NULL_WORLD ||
      target_.object == GNEISS_NULL_ENTITY_ID) {
    return gneiss::engine::result::invalid_state;
  }
  auto component = std::ranges::find_if(
      components_, [type_id](const auto& candidate) { return candidate.type_id == type_id; });
  if (component == components_.end()) {
    return gneiss::engine::result::not_found;
  }
  auto property = std::ranges::find(component->properties, field_id, &inspector_property::id);
  if (property == component->properties.end()) {
    return gneiss::engine::result::not_found;
  }
  if (!has_flags(property->capabilities, property_capabilities::writable)) {
    return gneiss::engine::result::unsupported;
  }
  const auto operation =
      gneiss::engine::write_property(registry_, type_id, field_id, target_, value);
  if (operation == gneiss::engine::result::success) {
    gneiss::engine::property_value refreshed;
    const auto read_operation =
        gneiss::engine::read_property(registry_, type_id, field_id, target_, refreshed);
    if (read_operation != gneiss::engine::result::success) {
      return read_operation;
    }
    property->value = std::move(refreshed);
  }
  return operation;
}

gneiss::engine::result
property_inspector_model::set_value(gneiss_world world_handle, entity_id entity,
                                    gneiss::engine::type_guid type_id, field_id field_id,
                                    const gneiss::engine::property_value& value) noexcept {
  if (!registry_ || world_handle == GNEISS_NULL_WORLD || !entity.is_valid()) {
    return gneiss::engine::result::invalid_argument;
  }
  const property_target target{.context = world_handle, .object = entity.get()};
  const auto operation =
      gneiss::engine::write_property(registry_, type_id, field_id, target, value);
  if (operation != gneiss::engine::result::success || target.context != target_.context ||
      target.object != target_.object) {
    return operation;
  }
  return refresh(world_handle, entity);
}

} // namespace gneiss::editor
