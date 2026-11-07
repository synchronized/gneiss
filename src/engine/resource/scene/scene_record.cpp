// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/resource/scene/scene_record.hpp"
#include "engine/core/serialization/value.hpp"
#include "engine/resource/scene/scene_validation.hpp"
namespace gneiss::engine {
bool component_override_record::operator==(const component_override_record&) const = default;
bool prefab_reference_record::operator==(const prefab_reference_record& other) const {
  if (uri != other.uri || components != other.components ||
      overrides.size() != other.overrides.size())
    return false;
  for (std::size_t i = 0; i < overrides.size(); ++i)
    if (overrides[i].key != other.overrides[i].key ||
        overrides[i].operation != other.overrides[i].operation)
      return false;
  return true;
}
bool object_record::operator==(const object_record& other) const {
  return uuid == other.uuid && parent == other.parent && name == other.name &&
         transform.translation == other.transform.translation &&
         transform.rotation == other.transform.rotation &&
         transform.scale == other.transform.scale && components == other.components &&
         prefab == other.prefab;
}
namespace {
result<component_record> capture_component(const component_description& source) {
  if (!source)
    return error::invalid_argument;
  auto content = serialization::capture_value(source);
  if (!content)
    return content.error().error;
  return component_record{source->guid(), std::move(*content)};
}
result<component_description> restore_component(const component_record& source) {
  auto restored = serialization::restore_value<component_description>(source.properties);
  if (!restored)
    return restored.error().error;
  if (!*restored || (*restored)->guid() != source.type)
    return error::invalid_argument;
  return std::move(*restored);
}
result<std::vector<object_record>> capture_objects(const std::vector<object_description>& source) {
  std::vector<object_record> output;
  output.reserve(source.size());
  for (const auto& object : source) {
    object_record record;
    record.uuid = object.uuid;
    record.parent = object.parent_uuid;
    record.name = object.name;
    record.transform = object.transform;
    for (const auto& component : object.components) {
      auto copied = capture_component(component);
      if (!copied)
        return copied.error();
      record.components.push_back(std::move(*copied));
    }
    if (object.prefab) {
      auto status = validate_prefab_reference_description(*object.prefab);
      if (status.error.failed())
        return status.error;
      record.prefab.emplace();
      record.prefab->uri = object.prefab->uri;
      record.prefab->overrides = object.prefab->overrides;
      for (const auto& entry : object.prefab->component_overrides) {
        component_override_record change;
        change.source_node_uuid = entry.source_node_uuid;
        change.type = entry.type_guid;
        if (entry.added) {
          auto copied = capture_component(*entry.added);
          if (!copied)
            return copied.error();
          change.added = std::move(*copied);
        }
        record.prefab->components.push_back(std::move(change));
      }
    }
    output.push_back(std::move(record));
  }
  return output;
}
result<std::vector<object_description>> restore_objects(const std::vector<object_record>& source) {
  std::vector<object_description> output;
  output.reserve(source.size());
  for (const auto& record : source) {
    object_description object;
    object.uuid = record.uuid;
    object.parent_uuid = record.parent;
    object.name = record.name;
    object.transform = record.transform;
    std::vector<component_description> components;
    for (const auto& component : record.components) {
      auto restored = restore_component(component);
      if (!restored)
        return restored.error();
      components.push_back(std::move(*restored));
    }
    auto status = object.components.replace(components);
    if (status.failed())
      return status;
    if (record.prefab) {
      object.prefab.emplace();
      object.prefab->uri = record.prefab->uri;
      object.prefab->overrides = record.prefab->overrides;
      for (const auto& entry : record.prefab->components) {
        prefab_component_override change;
        change.source_node_uuid = entry.source_node_uuid;
        change.type_guid = entry.type;
        if (entry.added) {
          auto restored = restore_component(*entry.added);
          if (!restored)
            return restored.error();
          change.added = std::move(*restored);
        }
        object.prefab->component_overrides.push_back(std::move(change));
      }
      auto status_prefab = validate_prefab_reference_description(*object.prefab);
      if (status_prefab.error.failed())
        return status_prefab.error;
    }
    auto status_object = validate_object_description(object);
    if (status_object.error.failed())
      return status_object.error;
    output.push_back(std::move(object));
  }
  return output;
}
}
result<scene_record> capture_scene_record(const scene_description& source) noexcept try {
  auto objects = capture_objects(source.objects);
  if (!objects)
    return objects.error();
  return scene_record{scene_record_data{source.uuid, std::move(*objects)}};
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<prefab_record> capture_prefab_record(const prefab_description& source) noexcept try {
  auto objects = capture_objects(source.objects);
  if (!objects)
    return objects.error();
  return prefab_record{prefab_record_data{source.uuid, source.root_uuid, std::move(*objects)}};
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<scene_description> restore_scene_record(const scene_record& source) noexcept try {
  auto objects = restore_objects(source.get().objects);
  if (!objects)
    return objects.error();
  scene_description result{source.get().uuid, std::move(*objects)};
  auto status = validate_scene_description(result);
  if (status.error.failed())
    return status.error;
  return result;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<prefab_description> restore_prefab_record(const prefab_record& source) noexcept try {
  auto objects = restore_objects(source.get().objects);
  if (!objects)
    return objects.error();
  prefab_description result{source.get().uuid, source.get().root_uuid, std::move(*objects)};
  auto status = validate_prefab_description(result);
  if (status.error.failed())
    return status.error;
  return result;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
}
