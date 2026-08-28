// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "property_inspector_model.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <new>
#include <utility>

namespace gneiss::editor {
namespace {

[[nodiscard]] bool type_id_equal(gneiss_type_id left, gneiss_type_id right) noexcept {
  return std::ranges::equal(left.bytes, right.bytes);
}

} // namespace

result property_inspector_model::initialize() noexcept {
  type_registry pending;
  auto operation = type_registry::create(pending);
  if (operation == result::success) {
    operation = world::register_reflection(pending);
  }
  if (operation == result::success) {
    operation = pending.freeze();
  }
  if (operation == result::success) {
    registry_ = std::move(pending);
  }
  return operation;
}

void property_inspector_model::clear() noexcept {
  components_.clear();
  target_ = {};
}

result property_inspector_model::refresh(gneiss_world world_handle, entity_id entity) noexcept {
  if (!registry_ || world_handle == GNEISS_NULL_WORLD || !entity.is_valid()) {
    clear();
    return result::invalid_argument;
  }
  try {
    const gneiss_property_target target{.context = world_handle, .object = entity.get()};
    std::vector<inspector_component> pending;
    const std::array reflected_component_types{gneiss_transform_type_id(), gneiss_camera_type_id()};
    for (const auto type_id : reflected_component_types) {
      gneiss_type_info type{};
      auto operation = registry_.find_type(type_id, type);
      if (operation != result::success) {
        clear();
        return operation;
      }
      inspector_component component{
          .type_id = type.id, .name = std::string(type.name, type.name_length), .properties = {}};
      bool component_missing = false;
      component.properties.reserve(type.field_count);
      for (std::uint32_t index = 0; index < type.field_count; ++index) {
        const auto& field = type.fields[index];
        gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
        operation = registry_.get_property(type.id, field.id, target, value);
        if (operation == result::not_found) {
          component_missing = true;
          break;
        }
        if (operation != result::success) {
          clear();
          return operation;
        }
        component.properties.push_back({.id = field.id,
                                        .name = std::string(field.name, field.name_length),
                                        .kind = field.property_kind,
                                        .capabilities = field.property_capabilities,
                                        .value = value});
      }
      if (!component_missing) {
        pending.push_back(std::move(component));
      }
    }
    components_ = std::move(pending);
    target_ = target;
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result property_inspector_model::set_value(gneiss_type_id type_id, gneiss_field_id field_id,
                                           const gneiss_property_value& value) noexcept {
  if (!registry_ || target_.context == GNEISS_NULL_WORLD ||
      target_.object == GNEISS_NULL_ENTITY_ID) {
    return result::invalid_state;
  }
  auto component = std::ranges::find_if(components_, [type_id](const auto& candidate) {
    return type_id_equal(candidate.type_id, type_id);
  });
  if (component == components_.end()) {
    return result::not_found;
  }
  auto property = std::ranges::find(component->properties, field_id, &inspector_property::id);
  if (property == component->properties.end()) {
    return result::not_found;
  }
  if ((property->capabilities & GNEISS_PROPERTY_CAPABILITY_WRITABLE) == 0U) {
    return result::unsupported;
  }
  const auto operation = registry_.set_property(type_id, field_id, target_, value);
  if (operation == result::success) {
    gneiss_property_value refreshed = GNEISS_PROPERTY_VALUE_INIT;
    const auto read_operation = registry_.get_property(type_id, field_id, target_, refreshed);
    if (read_operation != result::success) {
      return read_operation;
    }
    property->value = refreshed;
  }
  return operation;
}

result property_inspector_model::set_value(gneiss_world world_handle, entity_id entity,
                                           gneiss_type_id type_id, gneiss_field_id field_id,
                                           const gneiss_property_value& value) noexcept {
  if (!registry_ || world_handle == GNEISS_NULL_WORLD || !entity.is_valid()) {
    return result::invalid_argument;
  }
  const gneiss_property_target target{.context = world_handle, .object = entity.get()};
  const auto operation = registry_.set_property(type_id, field_id, target, value);
  if (operation != result::success || target.context != target_.context ||
      target.object != target_.object) {
    return operation;
  }
  return refresh(world_handle, entity);
}

} // namespace gneiss::editor
