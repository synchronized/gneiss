// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/value_edit.hpp"
#include <algorithm>
#include <functional>
namespace gneiss {
namespace {
using transform = std::function<result<value>(const value&)>;
result<value> replace_path(const value& root, std::span<const meta::property_step> path,
                           const transform& edit, std::size_t depth = 0) {
  if (depth > 64)
    return error::invalid_argument;
  if (path.empty())
    return edit(root);
  const auto& step = path.front();
  if (auto record = std::get_if<record_value>(&root.payload)) {
    const auto* id = std::get_if<meta::property_id>(&step);
    if (!id)
      return error::invalid_argument;
    auto data = record->get();
    auto field = std::ranges::find(data.properties, id->get(), &value_property::id);
    if (field == data.properties.end())
      return error::not_found;
    auto updated = replace_path(field->content, path.subspan(1), edit, depth + 1);
    if (!updated)
      return updated.error();
    field->content = std::move(*updated);
    return value{record_value{data}};
  }
  if (auto sequence = std::get_if<sequence_value>(&root.payload)) {
    auto data = sequence->get();
    std::size_t index;
    if (auto id = std::get_if<guid>(&step)) {
      if (!data.keyed || !*id)
        return error::invalid_argument;
      auto found = std::ranges::find(data.identities, *id);
      if (found == data.identities.end())
        return error::not_found;
      index = std::size_t(found - data.identities.begin());
    } else if (auto position = std::get_if<std::size_t>(&step)) {
      if (!data.fixed)
        return error::unsupported;
      index = *position;
    } else
      return error::invalid_argument;
    if (index >= data.elements.size())
      return error::not_found;
    auto updated = replace_path(data.elements[index], path.subspan(1), edit, depth + 1);
    if (!updated)
      return updated.error();
    data.elements[index] = std::move(*updated);
    return value{sequence_value{data}};
  }
  if (auto mapping = std::get_if<map_value>(&root.payload)) {
    auto key = std::get_if<std::string>(&step);
    if (!key)
      return error::invalid_argument;
    auto data = mapping->get();
    auto found = std::ranges::find(data.entries, value{*key}, &value_map_entry::key);
    if (found == data.entries.end())
      return error::not_found;
    auto updated = replace_path(found->content, path.subspan(1), edit, depth + 1);
    if (!updated)
      return updated.error();
    found->content = std::move(*updated);
    return value{map_value{data}};
  }
  return error::invalid_argument;
}
result<value> apply(const value& target, const value_operation& operation) {
  return std::visit(
      [&](const auto& op) -> result<value> {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, set_value_operation>)
          return op.content;
        else if constexpr (std::is_same_v<T, put_value_operation> ||
                           std::is_same_v<T, erase_key_operation>) {
          auto mapping = std::get_if<map_value>(&target.payload);
          if (!mapping)
            return error::invalid_argument;
          auto data = mapping->get();
          auto found = std::ranges::find(data.entries, op.key, &value_map_entry::key);
          if constexpr (std::is_same_v<T, put_value_operation>) {
            if (found == data.entries.end())
              data.entries.push_back({op.key, op.content});
            else
              found->content = op.content;
          } else if (found != data.entries.end())
            data.entries.erase(found);
          return value{map_value{data}};
        } else {
          auto sequence = std::get_if<sequence_value>(&target.payload);
          if (!sequence || !sequence->get().keyed || sequence->get().fixed || !op.element)
            return error::invalid_argument;
          auto data = sequence->get();
          auto found = std::ranges::find(data.identities, op.element);
          if constexpr (std::is_same_v<T, erase_value_operation>) {
            if (found != data.identities.end()) {
              auto index = found - data.identities.begin();
              data.elements.erase(data.elements.begin() + index);
              data.identities.erase(found);
            }
          } else {
            if (op.before == op.element)
              return error::invalid_argument;
            auto anchor = std::ranges::find(data.identities, op.before);
            if (op.before && anchor == data.identities.end())
              return error::not_found;
            value content;
            if constexpr (std::is_same_v<T, insert_value_operation>) {
              if (found != data.identities.end())
                return error::invalid_argument;
              content = op.content;
            } else {
              if (found == data.identities.end())
                return error::not_found;
              auto index = found - data.identities.begin();
              content = data.elements[std::size_t(index)];
              data.elements.erase(data.elements.begin() + index);
              data.identities.erase(found);
            }
            const auto position =
                op.before ? std::ranges::find(data.identities, op.before) : data.identities.end();
            const auto index = position - data.identities.begin();
            data.elements.insert(data.elements.begin() + index, std::move(content));
            data.identities.insert(position, op.element);
          }
          return value{sequence_value{data}};
        }
      },
      operation);
}
error diff(const value& before, const value& after, meta::property_path path,
           std::vector<value_edit>& edits, std::size_t depth) {
  if (depth > 64)
    return error::invalid_argument;
  if (before == after)
    return error::success;
  if (auto left = std::get_if<record_value>(&before.payload)) {
    auto right = std::get_if<record_value>(&after.payload);
    if (right && left->get().type == right->get().type &&
        left->get().properties.size() == right->get().properties.size()) {
      bool same_fields = true;
      for (const auto& field : left->get().properties)
        same_fields &= std::ranges::find(right->get().properties, field.id, &value_property::id) !=
                       right->get().properties.end();
      if (same_fields) {
        for (const auto& field : left->get().properties) {
          auto found = std::ranges::find(right->get().properties, field.id, &value_property::id);
          auto next = path;
          next.emplace_back(meta::property_id{field.id});
          auto status = diff(field.content, found->content, std::move(next), edits, depth + 1);
          if (status.failed())
            return status;
        }
        return error::success;
      }
    }
  }
  if (auto left = std::get_if<sequence_value>(&before.payload)) {
    auto right = std::get_if<sequence_value>(&after.payload);
    if (right && left->get().fixed && right->get().fixed &&
        left->get().elements.size() == right->get().elements.size() &&
        left->get().identities == right->get().identities &&
        left->get().keyed == right->get().keyed) {
      for (std::size_t i = 0; i < left->get().elements.size(); ++i) {
        auto next = path;
        next.emplace_back(i);
        auto status = diff(left->get().elements[i], right->get().elements[i], std::move(next),
                           edits, depth + 1);
        if (status.failed())
          return status;
      }
      return error::success;
    }
    if (right && left->get().keyed && right->get().keyed && !left->get().fixed &&
        !right->get().fixed) {
      auto ids = left->get().identities;
      for (auto id : left->get().identities)
        if (std::ranges::find(right->get().identities, id) == right->get().identities.end()) {
          edits.push_back({path, erase_value_operation{id}});
          std::erase(ids, id);
        }
      for (std::size_t i = 0; i < right->get().identities.size(); ++i) {
        auto id = right->get().identities[i];
        auto found = std::ranges::find(ids, id);
        auto anchor = i < ids.size() ? ids[i] : guid{};
        if (found == ids.end()) {
          edits.push_back({path, insert_value_operation{id, anchor, right->get().elements[i]}});
          ids.insert(ids.begin() + std::ptrdiff_t(i), id);
          continue;
        }
        if (std::size_t(found - ids.begin()) != i) {
          edits.push_back({path, move_value_operation{id, anchor}});
          ids.erase(found);
          ids.insert(ids.begin() + std::ptrdiff_t(i), id);
        }
        auto original =
            std::ranges::find(left->get().identities, id) - left->get().identities.begin();
        auto next = path;
        next.emplace_back(id);
        auto status = diff(left->get().elements[std::size_t(original)], right->get().elements[i],
                           std::move(next), edits, depth + 1);
        if (status.failed())
          return status;
      }
      return error::success;
    }
  }
  if (auto left = std::get_if<map_value>(&before.payload)) {
    if (auto right = std::get_if<map_value>(&after.payload)) {
      for (const auto& entry : left->get().entries)
        if (std::ranges::find(right->get().entries, entry.key, &value_map_entry::key) ==
            right->get().entries.end())
          edits.push_back({path, erase_key_operation{entry.key}});
      for (const auto& entry : right->get().entries) {
        auto old = std::ranges::find(left->get().entries, entry.key, &value_map_entry::key);
        if (old == left->get().entries.end() || old->content != entry.content)
          edits.push_back({path, put_value_operation{entry.key, entry.content}});
      }
      return error::success;
    }
  }
  edits.push_back({std::move(path), set_value_operation{after}});
  return error::success;
}
}
error validate_value_operation(const value_operation& op) {
  if (op.valueless_by_exception())
    return error::invalid_argument;
  return std::visit(
      [](const auto& item) -> error {
        using T = std::decay_t<decltype(item)>;
        if constexpr (requires { item.element; })
          if (!item.element)
            return error::invalid_argument;
        if constexpr (requires { item.before; })
          if (item.before == item.element)
            return error::invalid_argument;
        if constexpr (requires { item.key; }) {
          auto status =
              validate_persistent_value(value{map_value{map_value_data{{{item.key, value{}}}}}});
          if (status.failed())
            return status;
        }
        if constexpr (std::is_same_v<T, set_value_operation> ||
                      std::is_same_v<T, insert_value_operation> ||
                      std::is_same_v<T, put_value_operation>)
          return validate_persistent_value(item.content);
        return error::success;
      },
      op);
}
result<value, value_edit_error> apply_value_edits(const value& root,
                                                  std::span<const value_edit> edits) {
  auto status = validate_persistent_value(root);
  if (status.failed())
    return value_edit_error{0, status};
  auto candidate = root;
  for (std::size_t i = 0; i < edits.size(); ++i) {
    status = validate_value_operation(edits[i].operation);
    if (status.failed())
      return value_edit_error{i, status};
    auto updated = replace_path(candidate, edits[i].path, [&](const value& target) {
      return apply(target, edits[i].operation);
    });
    if (!updated)
      return value_edit_error{i, updated.error()};
    status = validate_persistent_value(*updated);
    if (status.failed())
      return value_edit_error{i, status};
    candidate = std::move(*updated);
  }
  return candidate;
}
result<value_edit_replay> replay_value_edits(const value& root, std::span<const value_edit> edits) {
  auto status = validate_persistent_value(root);
  if (status.failed())
    return status;
  value_edit_replay output{root, {}};
  for (std::size_t i = 0; i < edits.size(); ++i) {
    auto next = apply_value_edits(output.root, edits.subspan(i, 1));
    if (next)
      output.root = std::move(*next);
    else {
      if (next.error().error == error::out_of_memory || next.error().error == error::internal)
        return next.error().error;
      output.conflicts.push_back({i, next.error().error});
    }
  }
  return output;
}
result<std::vector<value_edit>> diff_values(const value& before, const value& after) {
  auto status = validate_persistent_value(before);
  if (status.ok())
    status = validate_persistent_value(after);
  if (status.failed())
    return status;
  std::vector<value_edit> edits;
  status = diff(before, after, {}, edits, 0);
  return status.ok() ? result<std::vector<value_edit>>{std::move(edits)}
                     : result<std::vector<value_edit>>{status};
}
}
