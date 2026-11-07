// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/resource/scene/scene_record.hpp"
#include "engine/core/serialization/value.hpp"
#include "engine/resource/scene/scene_validation.hpp"
#include <algorithm>
#include <map>
#include <set>
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
result<component_record> capture_component(const component_description& source, guid uuid) {
  if (!source || !uuid)
    return error::invalid_argument;
  auto content = serialization::capture_value(source);
  if (!content)
    return content.error().error;
  return component_record{uuid, source->guid(), std::move(*content)};
}
result<component_description> restore_component(const component_record& source) {
  auto restored = serialization::restore_value<component_description>(source.properties);
  if (!restored)
    return restored.error().error;
  if (!*restored || (*restored)->guid() != source.type)
    return error::invalid_argument;
  return std::move(*restored);
}
result<std::vector<object_record>>
capture_objects(const std::vector<game_object_description>& source) {
  std::vector<object_record> output;
  output.reserve(source.size());
  for (const auto& object : source) {
    object_record record;
    record.uuid = object.uuid;
    record.parent = object.parent_uuid;
    record.name = object.name;
    record.transform = object.transform;
    for (const auto& component : object.components) {
      auto copied = capture_component(component, object.components.identity(component->guid()));
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
        change.component_uuid = entry.component_uuid;
        change.type = entry.type_guid;
        if (entry.added) {
          auto copied = capture_component(*entry.added, entry.component_uuid);
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
result<std::vector<game_object_description>>
restore_objects(const std::vector<object_record>& source) {
  std::vector<game_object_description> output;
  output.reserve(source.size());
  for (const auto& record : source) {
    game_object_description object;
    object.uuid = record.uuid;
    object.parent_uuid = record.parent;
    object.name = record.name;
    object.transform = record.transform;
    std::vector<component_entry> components;
    for (const auto& component : record.components) {
      auto restored = restore_component(component);
      if (!restored)
        return restored.error();
      components.push_back({component.uuid, std::move(*restored)});
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
        change.component_uuid = entry.component_uuid;
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
    auto status_object = validate_game_object_description(object);
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
namespace {
struct record_remapper {
  guid source_document;
  guid target_document;
  const std::map<guid, guid>& objects;
  mutable error failure{error::success};
  value rewrite(const value& source) const {
    return std::visit(
        [&](const auto& item) -> value {
          using T = std::decay_t<decltype(item)>;
          if constexpr (std::is_same_v<T, object_reference_value>) {
            auto copied = item;
            if (item.document == source_document) {
              if (auto found = objects.find(item.object); found != objects.end()) {
                copied.document = target_document;
                copied.object = found->second;
              }
            }
            return value{copied};
          } else if constexpr (std::is_same_v<T, record_value>) {
            // 反射捕获的显式引用结构也按类型恢复，避免硬编码字段编号。
            if (item.get().type == object_reference_value::type_guid) {
              auto reference = serialization::restore_value<object_reference_value>(value{item});
              if (!reference) {
                failure = reference.error().error;
                return source;
              }
              auto changed = rewrite(value{*reference});
              auto captured =
                  serialization::capture_value(std::get<object_reference_value>(changed.payload));
              if (!captured) {
                failure = captured.error().error;
                return source;
              }
              return std::move(*captured);
            }
            auto copied = item.get();
            for (auto& field : copied.properties)
              field.content = rewrite(field.content);
            return value{record_value{copied}};
          } else if constexpr (std::is_same_v<T, sequence_value>) {
            auto copied = item.get();
            for (auto& element : copied.elements)
              element = rewrite(element);
            return value{sequence_value{copied}};
          } else if constexpr (std::is_same_v<T, map_value>) {
            auto copied = item.get();
            for (auto& entry : copied.entries) {
              entry.key = rewrite(entry.key);
              entry.content = rewrite(entry.content);
            }
            return value{map_value{copied}};
          } else if constexpr (std::is_same_v<T, optional_value>) {
            auto copied = item.get();
            if (copied.content)
              copied.content = rewrite(*copied.content);
            return value{optional_value{copied}};
          } else if constexpr (std::is_same_v<T, variant_value>) {
            auto copied = item.get();
            copied.content = rewrite(copied.content);
            return value{variant_value{copied}};
          } else
            return value{item};
        },
        source.payload);
  }
  error component(component_record& component) const {
    auto status = validate_persistent_value(component.properties);
    if (status.failed())
      return status;
    component.uuid = guid::generate();
    component.properties = rewrite(component.properties);
    return failure.failed() ? failure : validate_persistent_value(component.properties);
  }
  error operation(value_operation& operation) const {
    auto status = validate_value_operation(operation);
    if (status.failed())
      return status;
    std::visit(
        [&](auto& item) {
          if constexpr (requires { item.content; })
            item.content = rewrite(item.content);
          if constexpr (requires { item.key; })
            item.key = rewrite(item.key);
        },
        operation);
    return failure.failed() ? failure : validate_value_operation(operation);
  }
};
}
result<scene_record> remap_scene_record(const scene_record& source, guid target_document,
                                        std::span<const uuid_mapping> mappings) noexcept try {
  const auto& original = source.get();
  if (!original.uuid || !target_document || mappings.size() != original.objects.size())
    return error::invalid_argument;
  std::map<guid, guid> objects;
  std::set<guid> targets;
  for (const auto& entry : mappings)
    if (!entry.source_uuid || !entry.target_uuid ||
        !objects.emplace(entry.source_uuid, entry.target_uuid).second ||
        !targets.insert(entry.target_uuid).second)
      return error::invalid_argument;
  std::set<guid> sources;
  for (const auto& object : original.objects)
    if (!objects.contains(object.uuid) || !sources.insert(object.uuid).second ||
        (object.parent && !objects.contains(object.parent)))
      return error::invalid_argument;
  auto candidate = original;
  candidate.uuid = target_document;
  record_remapper remapper{original.uuid, target_document, objects};
  for (auto& object : candidate.objects) {
    object.uuid = objects.at(object.uuid);
    if (object.parent)
      object.parent = objects.at(object.parent);
    for (auto& component : object.components) {
      auto status = remapper.component(component);
      if (status.failed())
        return status;
    }
    if (object.prefab) {
      for (auto& entry : object.prefab->overrides) {
        auto status = remapper.operation(entry.operation);
        if (status.failed())
          return status;
      }
      for (auto& entry : object.prefab->components)
        if (entry.added) {
          auto status = remapper.component(*entry.added);
          if (status.failed())
            return status;
          entry.component_uuid = entry.added->uuid;
        }
    }
  }
  return scene_record{candidate};
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

namespace {
struct reference_inspector {
  document_object_view document;
  std::span<const document_object_view> external;
  std::vector<object_reference_issue> issues;
  guid object, component;
  void reference(const object_reference_value& target) {
    if (!target.document && !target.object)
      return;
    auto status = error{error::success};
    if (!target.document || !target.object) {
      status = error::invalid_argument;
    } else {
      const document_object_view* source = &document;
      if (target.document != document.document) {
        auto found = std::ranges::find(external, target.document, &document_object_view::document);
        source = found == external.end() ? nullptr : &*found;
      }
      if (!source)
        status = error::not_ready;
      else if (std::ranges::find(source->objects, target.object, &object_record::uuid) ==
               source->objects.end())
        status = error::not_found;
    }
    if (status.failed())
      issues.push_back({document.document, object, component, target, status});
  }
  error inspect(const value& source) {
    return std::visit(
        [&](const auto& item) -> error {
          using T = std::decay_t<decltype(item)>;
          if constexpr (std::is_same_v<T, object_reference_value>) {
            reference(item);
          } else if constexpr (std::is_same_v<T, record_value>) {
            if (item.get().type == object_reference_value::type_guid) {
              auto decoded = serialization::restore_value<object_reference_value>(source);
              if (!decoded)
                return decoded.error().error;
              reference(*decoded);
            } else {
              for (const auto& property : item.get().properties) {
                auto status = inspect(property.content);
                if (status.failed())
                  return status;
              }
            }
          } else if constexpr (std::is_same_v<T, sequence_value>) {
            for (const auto& element : item.get().elements) {
              auto status = inspect(element);
              if (status.failed())
                return status;
            }
          } else if constexpr (std::is_same_v<T, map_value>) {
            for (const auto& entry : item.get().entries) {
              auto status = inspect(entry.key);
              if (status.ok())
                status = inspect(entry.content);
              if (status.failed())
                return status;
            }
          } else if constexpr (std::is_same_v<T, optional_value>) {
            if (item.get().content)
              return inspect(*item.get().content);
          } else if constexpr (std::is_same_v<T, variant_value>) {
            return inspect(item.get().content);
          }
          return error::success;
        },
        source.payload);
  }
};
}
result<std::vector<object_reference_issue>>
inspect_object_references(document_object_view document,
                          std::span<const document_object_view> external) noexcept try {
  reference_inspector inspector{document, external, {}, {}, {}};
  for (const auto& object : document.objects) {
    inspector.object = object.uuid;
    for (const auto& component : object.components) {
      inspector.component = component.uuid;
      auto status = inspector.inspect(component.properties);
      if (status.failed())
        return status;
    }
    if (!object.prefab)
      continue;
    for (const auto& component : object.prefab->components) {
      if (!component.added)
        continue;
      inspector.component = component.component_uuid;
      auto status = inspector.inspect(component.added->properties);
      if (status.failed())
        return status;
    }
    for (const auto& entry : object.prefab->overrides) {
      inspector.component = entry.key.target.component_uuid;
      auto status = std::visit(
          [&](const auto& operation) -> error {
            if constexpr (requires { operation.content; }) {
              auto status = inspector.inspect(operation.content);
              if (status.failed())
                return status;
            }
            if constexpr (requires { operation.key; })
              return inspector.inspect(operation.key);
            return error::success;
          },
          entry.operation);
      if (status.failed())
        return status;
    }
  }
  return std::move(inspector.issues);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
}
