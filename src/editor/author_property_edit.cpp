// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_property_edit.hpp"

#include <memory>
#include <new>
#include <string>
#include <utility>

namespace gneiss::editor {

namespace {
result write_author_property(editor_session& session, property_inspector_model& inspector,
                             gneiss_world world, std::string_view uuid, gneiss_type_id type_id,
                             gneiss_field_id field_id,
                             const gneiss_property_value& value) noexcept {
  const auto* current = session.find_node(uuid);
  if (current == nullptr) {
    return result::not_found;
  }
  const auto operation = inspector.set_value(world, current->entity, type_id, field_id, value);
  if (operation.ok()) {
    session.mark_dirty();
  }
  return operation;
}
} // namespace

result edit_author_property(editor_session& session, editor_command_history& history,
                            property_inspector_model& inspector, gneiss_world world,
                            const inspector_component& component,
                            const inspector_property& property, const gneiss_property_value& value,
                            std::uint64_t edit_serial) noexcept try {
  const auto* selected = session.selected_node();
  if (selected == nullptr) {
    return result::invalid_state;
  }
  const auto uuid = selected->uuid;
  const auto type_id = component.type_id;
  const auto field_id = property.id;
  const auto previous = property.value;
  std::string merge_key = "property:" + uuid;
  merge_key.append(reinterpret_cast<const char*>(type_id.bytes), sizeof(type_id.bytes));
  merge_key.append(reinterpret_cast<const char*>(&field_id), sizeof(field_id));
  merge_key.append(reinterpret_cast<const char*>(&edit_serial), sizeof(edit_serial));
  // 命令对象分配先于写入，避免分配失败留下未记录的属性变更。
  auto command = std::make_unique<functional_editor_command>(editor_command_history::command{
      .label = std::string{"修改 "} + property.name,
      .undo =
          [&session, &inspector, world, uuid, type_id, field_id, previous]() noexcept {
            return write_author_property(session, inspector, world, uuid, type_id, field_id,
                                         previous);
          },
      .redo =
          [&session, &inspector, world, uuid, type_id, field_id, value]() noexcept {
            return write_author_property(session, inspector, world, uuid, type_id, field_id, value);
          },
      .merge_key = std::move(merge_key),
  });
  const auto operation = inspector.set_value(type_id, field_id, value);
  if (operation.failed()) {
    return operation;
  }
  const auto recorded = history.record(std::move(command));
  if (recorded.failed()) {
    (void)inspector.set_value(type_id, field_id, previous);
    return recorded;
  }
  session.mark_dirty();
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

} // namespace gneiss::editor
