// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/document_patch.hpp"
#include <algorithm>
namespace gneiss::editor {
namespace {
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
      if (!(old[i] == *found) || i != index)
        result.objects.push_back({old[i].uuid, old[i], *found, i, index});
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
  auto result = a;
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
