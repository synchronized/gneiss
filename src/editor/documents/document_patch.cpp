// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/document_patch.hpp"
#include <algorithm>
namespace gneiss::editor {
namespace {
bool equal_field(const engine::math::transform& a, const engine::math::transform& b) {
  return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}
template <class T> bool equal_field(const T& a, const T& b) { return a == b; }
template <class T> auto diff_field(const T& a, const T& b) {
  return equal_field(a, b) ? std::optional<document_field_change<T>>{}
                           : std::optional<document_field_change<T>>{{a, b}};
}
template <class T>
bool apply_field(T& field, const std::optional<document_field_change<T>>& change, bool undo) {
  if (!change)
    return true;
  if (!equal_field(field, undo ? change->after : change->before))
    return false;
  field = undo ? change->before : change->after;
  return true;
}
template <class T>
bool merge_field(std::optional<document_field_change<T>>& field,
                 const std::optional<document_field_change<T>>& change) {
  if (!change)
    return true;
  if (!field) {
    field = change;
    return true;
  }
  if (!equal_field(field->after, change->before))
    return false;
  field->after = change->after;
  return true;
}
// 记录字段递归到叶值；没有稳定身份的容器作为一个属性整体替换。
void diff_properties(const engine::value& a, const engine::value& b,
                     engine::meta::property_path path, std::vector<document_value_change>& out) {
  if (a == b)
    return;
  const auto* left = std::get_if<engine::record_value>(&a.payload);
  const auto* right = std::get_if<engine::record_value>(&b.payload);
  if (left && right && left->get().type == right->get().type &&
      left->get().properties.size() == right->get().properties.size() &&
      std::ranges::all_of(left->get().properties, [&](const auto& field) {
        return std::ranges::find(right->get().properties, field.id, &engine::value_property::id) !=
               right->get().properties.end();
      })) {
    for (const auto& field : left->get().properties) {
      auto target =
          std::ranges::find(right->get().properties, field.id, &engine::value_property::id);
      auto child = path;
      child.emplace_back(engine::meta::property_id{field.id});
      diff_properties(field.content, target->content, std::move(child), out);
    }
  } else
    out.push_back({std::move(path), a, b});
}
const engine::value* property_value(const engine::value& root,
                                    const engine::meta::property_path& path) {
  const auto* value = &root;
  for (const auto& step : path) {
    const auto* id = std::get_if<engine::meta::property_id>(&step);
    const auto* record = std::get_if<engine::record_value>(&value->payload);
    if (!id || !record)
      return nullptr;
    auto found =
        std::ranges::find(record->get().properties, id->get(), &engine::value_property::id);
    if (found == record->get().properties.end())
      return nullptr;
    value = &found->content;
  }
  return value;
}
std::optional<document_component_changes>
diff_components(const std::vector<engine::component_record>& a,
                const std::vector<engine::component_record>& b) {
  if (a == b)
    return {};
  document_component_changes result;
  for (std::size_t i = 0; i < a.size(); ++i) {
    auto found = std::ranges::find(b, a[i].uuid, &engine::component_record::uuid);
    if (found == b.end())
      result.objects.push_back({a[i].uuid, a[i], {}, i, 0});
    else if (a[i] != *found || i != std::size_t(found - b.begin())) {
      auto index = std::size_t(found - b.begin());
      if (i != index || a[i].type != found->type)
        result.objects.push_back({a[i].uuid, a[i], *found, i, index});
      else {
        document_component_update update{a[i].uuid, a[i].type, {}};
        diff_properties(a[i].properties, found->properties, {}, update.properties);
        result.updates.push_back(std::move(update));
      }
    }
  }
  for (std::size_t i = 0; i < b.size(); ++i)
    if (std::ranges::find(a, b[i].uuid, &engine::component_record::uuid) == a.end())
      result.objects.push_back({b[i].uuid, {}, b[i], 0, i});
  return result;
}
engine::error apply_components(std::vector<engine::component_record>& values,
                               const std::optional<document_component_changes>& changes,
                               bool undo) {
  if (!changes)
    return engine::error::success;
  for (const auto& update : changes->updates) {
    auto found = std::ranges::find(values, update.uuid, &engine::component_record::uuid);
    if (found == values.end() || found->type != update.type)
      return engine::error::invalid_state;
    std::vector<engine::value_edit> edits;
    for (const auto& property : update.properties) {
      const auto* current = property_value(found->properties, property.path);
      if (!current || *current != (undo ? property.after : property.before))
        return engine::error::invalid_state;
      edits.push_back(
          {property.path, engine::set_value_operation{undo ? property.before : property.after}});
    }
    auto applied = engine::apply_value_edits(found->properties, edits);
    if (!applied)
      return applied.error().error;
    found->properties = std::move(*applied);
  }
  for (const auto& change : changes->objects) {
    const auto& expected = undo ? change.after : change.before;
    auto found = std::ranges::find(values, change.uuid, &engine::component_record::uuid);
    if (expected ? found == values.end() || *found != *expected ||
                       std::size_t(found - values.begin()) !=
                           (undo ? change.after_index : change.before_index)
                 : found != values.end())
      return engine::error::invalid_state;
  }
  std::erase_if(values, [&](const auto& item) {
    return std::ranges::find(changes->objects, item.uuid, &document_component_change::uuid) !=
           changes->objects.end();
  });
  std::vector<std::pair<std::size_t, engine::component_record>> insertions;
  for (const auto& change : changes->objects) {
    const auto& target = undo ? change.before : change.after;
    if (target)
      insertions.emplace_back(undo ? change.before_index : change.after_index, *target);
  }
  std::ranges::sort(insertions, {}, &decltype(insertions)::value_type::first);
  for (auto& [index, item] : insertions) {
    if (index > values.size())
      return engine::error::invalid_state;
    values.insert(values.begin() + index, std::move(item));
  }
  return engine::error::success;
}
bool merge_components(std::optional<document_component_changes>& a,
                      const std::optional<document_component_changes>& b) {
  if (!b)
    return true;
  if (!a) {
    a = b;
    return true;
  }
  if (!a->objects.empty() || !b->objects.empty())
    return false;
  for (const auto& update : b->updates) {
    auto found = std::ranges::find(a->updates, update.uuid, &document_component_update::uuid);
    if (found == a->updates.end()) {
      a->updates.push_back(update);
      continue;
    }
    if (found->type != update.type)
      return false;
    for (const auto& property : update.properties) {
      auto old = std::ranges::find(found->properties, property.path, &document_value_change::path);
      if (old == found->properties.end()) {
        // 父路径整体替换和子路径变更不混合合并。
        for (const auto& existing : found->properties) {
          auto count = std::min(existing.path.size(), property.path.size());
          if (std::equal(existing.path.begin(), existing.path.begin() + count,
                         property.path.begin()))
            return false;
        }
        found->properties.push_back(property);
      } else {
        if (old->after != property.before)
          return false;
        old->after = property.after;
      }
    }
  }
  return true;
}
template <class Map, class Changes> void diff_map(const Map& a, const Map& b, Changes& out) {
  for (const auto& [key, value] : a) {
    auto found = b.find(key);
    if (found == b.end())
      out.push_back({key, value, {}});
    else if (!(value == found->second))
      out.push_back({key, value, found->second});
  }
  for (const auto& [key, value] : b)
    if (!a.contains(key))
      out.push_back({key, {}, value});
}
template <class Map, class Changes> bool patch_map(Map& values, const Changes& changes, bool undo) {
  for (const auto& change : changes) {
    const auto& expected = undo ? change.after : change.before;
    const auto& target = undo ? change.before : change.after;
    auto found = values.find(change.key);
    if (expected ? found == values.end() || !(found->second == *expected) : found != values.end())
      return false;
    if (target)
      values.insert_or_assign(change.key, *target);
    else
      values.erase(change.key);
  }
  return true;
}
}
engine::result<document_patch> make_document_patch(const scene_document_snapshot& a,
                                                   const scene_document_snapshot& b) noexcept try {
  if (a.uri != b.uri || a.content.get().uuid != b.content.get().uuid)
    return engine::error::invalid_argument;
  document_patch result;
  result.document = a.content.get().uuid;
  result.uri = a.uri;
  const auto& old = a.content.get().objects;
  const auto& next = b.content.get().objects;
  for (std::size_t i = 0; i < old.size(); ++i) {
    auto found = std::ranges::find(next, old[i].uuid, &engine::object_record::uuid);
    if (found == next.end())
      result.objects.push_back({old[i].uuid, old[i], {}, i, 0});
    else {
      const auto index = static_cast<std::size_t>(found - next.begin());
      if (i != index)
        result.objects.push_back({old[i].uuid, old[i], *found, i, index});
      else if (!(old[i] == *found))
        result.updates.push_back({old[i].uuid, diff_field(old[i].parent, found->parent),
                                  diff_field(old[i].name, found->name),
                                  diff_field(old[i].transform, found->transform),
                                  diff_components(old[i].components, found->components),
                                  diff_field(old[i].prefab, found->prefab)});
    }
  }
  for (std::size_t i = 0; i < next.size(); ++i)
    if (std::ranges::find(old, next[i].uuid, &engine::object_record::uuid) == old.end())
      result.objects.push_back({next[i].uuid, {}, next[i], 0, i});
  diff_map(a.prefabs, b.prefabs, result.prefabs);
  diff_map(a.assets, b.assets, result.assets);
  result.source_changed = a.source_asset != b.source_asset;
  if (result.source_changed) {
    result.before_source = a.source_asset;
    result.after_source = b.source_asset;
  }
  return result;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<document_patch> merge_document_patches(const document_patch& a,
                                                      const document_patch& b) noexcept try {
  if (a.document != b.document || a.uri != b.uri)
    return engine::error::invalid_state;
  // 结构与属性跨形态合并不猜测中间对象；交由历史保留独立条目。
  for (const auto& update : a.updates)
    if (std::ranges::find(b.objects, update.uuid, &document_object_change::uuid) != b.objects.end())
      return engine::error::invalid_state;
  for (const auto& update : b.updates)
    if (std::ranges::find(a.objects, update.uuid, &document_object_change::uuid) != a.objects.end())
      return engine::error::invalid_state;
  auto result = a;
  for (const auto& update : b.updates) {
    auto found = std::ranges::find(result.updates, update.uuid, &document_object_update::uuid);
    if (found == result.updates.end())
      result.updates.push_back(update);
    else if (!merge_field(found->parent, update.parent) || !merge_field(found->name, update.name) ||
             !merge_field(found->transform, update.transform) ||
             !merge_components(found->components, update.components) ||
             !merge_field(found->prefab, update.prefab))
      return engine::error::invalid_state;
  }
  for (const auto& change : b.objects) {
    auto found = std::ranges::find(result.objects, change.uuid, &document_object_change::uuid);
    if (found == result.objects.end())
      result.objects.push_back(change);
    else {
      if (found->after != change.before ||
          (found->after && found->after_index != change.before_index))
        return engine::error::invalid_state;
      found->after = change.after;
      found->after_index = change.after_index;
    }
  }
  const auto merge_map = [](auto& target, const auto& changes) {
    for (const auto& change : changes) {
      auto found = std::ranges::find_if(target, [&](const auto& v) { return v.key == change.key; });
      if (found == target.end())
        target.push_back(change);
      else {
        if (found->after != change.before)
          return false;
        found->after = change.after;
      }
    }
    return true;
  };
  if (!merge_map(result.prefabs, b.prefabs) || !merge_map(result.assets, b.assets))
    return engine::error::invalid_state;
  if (b.source_changed) {
    if (result.source_changed && result.after_source != b.before_source)
      return engine::error::invalid_state;
    if (!result.source_changed)
      result.before_source = b.before_source;
    result.after_source = b.after_source;
    result.source_changed = true;
  }
  return result;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<scene_document_snapshot> apply_document_patch(const scene_document_snapshot& input,
                                                             const document_patch& patch,
                                                             bool undo) noexcept try {
  if (input.uri != patch.uri || input.content.get().uuid != patch.document)
    return engine::error::invalid_state;
  auto result = input;
  auto data = input.content.get();
  for (const auto& update : patch.updates) {
    auto found = std::ranges::find(data.objects, update.uuid, &engine::object_record::uuid);
    if (found == data.objects.end() || !apply_field(found->parent, update.parent, undo) ||
        !apply_field(found->name, update.name, undo) ||
        !apply_field(found->transform, update.transform, undo) ||
        !apply_field(found->prefab, update.prefab, undo))
      return engine::error::invalid_state;
    auto status = apply_components(found->components, update.components, undo);
    if (status.failed())
      return status;
  }
  for (const auto& change : patch.objects) {
    const auto& expected = undo ? change.after : change.before;
    auto found = std::ranges::find(data.objects, change.uuid, &engine::object_record::uuid);
    if (expected) {
      if (found == data.objects.end() || !(*found == *expected) ||
          static_cast<std::size_t>(found - data.objects.begin()) !=
              (undo ? change.after_index : change.before_index))
        return engine::error::invalid_state;
    } else if (found != data.objects.end())
      return engine::error::invalid_state;
  }
  std::erase_if(data.objects, [&](const auto& object) {
    return std::ranges::find(patch.objects, object.uuid, &document_object_change::uuid) !=
           patch.objects.end();
  });
  std::vector<std::pair<std::size_t, engine::object_record>> insertions;
  for (const auto& change : patch.objects) {
    const auto& target = undo ? change.before : change.after;
    if (target)
      insertions.emplace_back(undo ? change.before_index : change.after_index, *target);
  }
  std::ranges::sort(insertions, {}, &decltype(insertions)::value_type::first);
  for (auto& [index, object] : insertions) {
    if (index > data.objects.size())
      return engine::error::invalid_state;
    data.objects.insert(data.objects.begin() + index, std::move(object));
  }
  if (!patch_map(result.prefabs, patch.prefabs, undo) ||
      !patch_map(result.assets, patch.assets, undo))
    return engine::error::invalid_state;
  if (patch.source_changed) {
    if (result.source_asset != (undo ? patch.after_source : patch.before_source))
      return engine::error::invalid_state;
    result.source_asset = undo ? patch.before_source : patch.after_source;
  }
  result.content = engine::scene_record{std::move(data)};
  auto valid = prepare_scene_document(result, {});
  if (!valid)
    return valid.error();
  return result;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
}
