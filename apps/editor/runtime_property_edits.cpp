// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_property_edits.h"

#include <algorithm>
#include <new>
#include <utility>

namespace gneiss::editor {

void runtime_property_edits::begin_session(std::uint64_t session_id) noexcept {
  if (session_id != 0U && session_id_ == session_id) {
    return;
  }
  edits_.clear();
  pending_.clear();
  session_id_ = session_id;
  next_command_id_ = 1U;
}

void runtime_property_edits::disconnect() noexcept {
  for (const auto& [command_id, pending] : pending_) {
    (void)command_id;
    auto found = edits_.find(pending.key);
    if (found != edits_.end()) {
      found->second.state = runtime_property_edit_state::disconnected;
      found->second.code = GNEISS_ERROR_IO;
      found->second.message = "Runtime 连接已断开";
    }
  }
  pending_.clear();
  session_id_ = 0U;
}

result runtime_property_edits::prepare(runtime_property_key key, std::uint64_t expected_revision,
                                       ipc_property_value value, clock::time_point now,
                                       ipc_property_write& output) noexcept {
  if (session_id_ == 0U || !key.object.is_valid() || key.field_id == GNEISS_NULL_FIELD_ID ||
      expected_revision == 0U || next_command_id_ == 0U) {
    return result::invalid_state;
  }
  const auto existing = edits_.find(key);
  if (existing != edits_.end() && existing->second.state == runtime_property_edit_state::pending) {
    return result::not_ready;
  }
  try {
    const auto command_id = next_command_id_++;
    ipc_property_write command{.session_id = session_id_,
                               .command_id = command_id,
                               .object = key.object,
                               .type_id = {},
                               .field_id = key.field_id,
                               .expected_revision = expected_revision,
                               .value = std::move(value)};
    std::ranges::copy(key.type_id, command.type_id.bytes);
    edits_[key] = {.state = runtime_property_edit_state::pending,
                   .command_id = command_id,
                   .revision = expected_revision,
                   .code = GNEISS_SUCCESS,
                   .message = "等待 Runtime 确认",
                   .canonical_value = {}};
    pending_.emplace(command_id, pending_edit{key, now});
    output = std::move(command);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result runtime_property_edits::accept(ipc_property_write_result response) noexcept {
  if (response.session_id != session_id_) {
    return result::invalid_state;
  }
  const auto pending = pending_.find(response.command_id);
  if (pending == pending_.end()) {
    return result::not_found;
  }
  auto edit = edits_.find(pending->second.key);
  if (edit == edits_.end() || edit->second.command_id != response.command_id) {
    pending_.erase(pending);
    return result::not_found;
  }
  edit->second.state = response.code == GNEISS_SUCCESS ? runtime_property_edit_state::applied
                                                       : runtime_property_edit_state::rejected;
  edit->second.revision = response.revision;
  edit->second.code = response.code;
  edit->second.message = std::move(response.message);
  edit->second.canonical_value = std::move(response.canonical_value);
  pending_.erase(pending);
  return result::success;
}

void runtime_property_edits::expire(clock::time_point now,
                                    std::chrono::milliseconds timeout) noexcept {
  for (auto current = pending_.begin(); current != pending_.end();) {
    if (now - current->second.sent_at < timeout) {
      ++current;
      continue;
    }
    auto edit = edits_.find(current->second.key);
    if (edit != edits_.end()) {
      edit->second.state = runtime_property_edit_state::timed_out;
      edit->second.code = GNEISS_ERROR_NOT_READY;
      edit->second.message = "等待 Runtime 响应超时";
    }
    current = pending_.erase(current);
  }
}

const runtime_property_edit*
runtime_property_edits::find(const runtime_property_key& key) const noexcept {
  const auto found = edits_.find(key);
  return found == edits_.end() ? nullptr : &found->second;
}

} // namespace gneiss::editor
