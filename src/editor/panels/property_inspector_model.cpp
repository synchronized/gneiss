// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "panels/property_inspector_model.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <new>
#include <utility>

namespace gneiss::editor {
gneiss::engine::error property_inspector_model::initialize() noexcept {
  return gneiss::engine::create_property_registry(registry_);
}

void property_inspector_model::clear() noexcept {
  components_.clear();
  world_ = {};
  entity_ = {};
}

gneiss::engine::error property_inspector_model::refresh(gneiss::engine::world* world_handle,
                                                        entity_id entity) noexcept {
  if (!registry_ || world_handle == nullptr || !entity.is_valid()) {
    clear();
    return gneiss::engine::error::invalid_argument;
  }
  try {
    std::vector<inspector_component> pending;
    const std::array reflected_component_types{
        gneiss::engine::transform_property_type(),
        gneiss::engine::camera_property_type(),
    };
    for (const auto type_id : reflected_component_types) {
      gneiss::engine::meta::type_info type{};
      auto operation = gneiss::engine::meta::describe_type(registry_, type_id, type);
      if (operation != gneiss::engine::error::success) {
        clear();
        return operation;
      }
      inspector_component component{
          .type_id = type.guid,
          .name = std::string(type.name),
          .properties = {},
      };
      bool component_missing = false;
      component.properties.reserve(type.properties.size());
      for (std::uint32_t index = 0; index < type.properties.size(); ++index) {
        const auto& field = type.properties[index];
        gneiss::engine::value value;
        operation = gneiss::engine::read_component_property(registry_, *world_handle, entity.get(),
                                                            type.guid, field.id, value);
        if (operation == gneiss::engine::error::not_found) {
          component_missing = true;
          break;
        }
        if (operation != gneiss::engine::error::success) {
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
    world_ = world_handle;
    entity_ = entity;
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error
property_inspector_model::set_value(gneiss::engine::guid type_id,
                                    gneiss::engine::meta::property_id field_id,
                                    const gneiss::engine::value& value) noexcept {
  if (!registry_ || world_ == nullptr || entity_.get() == UINT64_C(0)) {
    return gneiss::engine::error::invalid_state;
  }
  auto component = std::ranges::find_if(
      components_, [type_id](const auto& candidate) { return candidate.type_id == type_id; });
  if (component == components_.end()) {
    return gneiss::engine::error::not_found;
  }
  auto property = std::ranges::find(component->properties, field_id, &inspector_property::id);
  if (property == component->properties.end()) {
    return gneiss::engine::error::not_found;
  }
  if (!has_flags(property->capabilities, gneiss::engine::meta::property_capabilities::writable)) {
    return gneiss::engine::error::unsupported;
  }
  const auto operation = gneiss::engine::write_component_property(registry_, *world_, entity_.get(),
                                                                  type_id, field_id, value);
  if (operation == gneiss::engine::error::success) {
    gneiss::engine::value refreshed;
    const auto read_operation = gneiss::engine::read_component_property(
        registry_, *world_, entity_.get(), type_id, field_id, refreshed);
    if (read_operation != gneiss::engine::error::success) {
      return read_operation;
    }
    property->value = std::move(refreshed);
  }
  return operation;
}

gneiss::engine::error property_inspector_model::set_value(
    gneiss::engine::world* world_handle, entity_id entity, gneiss::engine::guid type_id,
    gneiss::engine::meta::property_id field_id, const gneiss::engine::value& value) noexcept {
  if (!registry_ || world_handle == nullptr || !entity.is_valid()) {
    return gneiss::engine::error::invalid_argument;
  }
  const auto operation = gneiss::engine::write_component_property(
      registry_, *world_handle, entity.get(), type_id, field_id, value);
  if (operation != gneiss::engine::error::success || world_handle != world_ ||
      entity.get() != entity_.get()) {
    return operation;
  }
  return refresh(world_handle, entity);
}

} // namespace gneiss::editor
