// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_property_edits.h"

#include <array>
#include <chrono>

namespace {

gneiss::editor::runtime_property_key make_key() {
  return {.object = {2U, 3U}, .type_id = {1U}, .field_id = 4U};
}

bool test_result_and_single_pending() {
  gneiss::editor::runtime_property_edits edits;
  edits.begin_session(7U);
  const auto now = gneiss::editor::runtime_property_edits::clock::now();
  gneiss::ipc_property_write command;
  const auto key = make_key();
  if (edits.prepare(key, 1U, {std::array<float, 3>{1.0F, 2.0F, 3.0F}}, now, command) !=
          gneiss::result::success ||
      command.session_id != 7U || command.command_id != 1U ||
      edits.prepare(key, 1U, {true}, now, command) != gneiss::result::not_ready) {
    return false;
  }
  const gneiss::ipc_property_write_result response{
      .session_id = 7U,
      .command_id = 1U,
      .code = GNEISS_SUCCESS,
      .revision = 2U,
      .message = "属性已应用",
      .canonical_value = {std::array<float, 3>{1.0F, 2.0F, 3.0F}}};
  if (edits.accept(response) != gneiss::result::success) {
    return false;
  }
  const auto* state = edits.find(key);
  if (state == nullptr || state->state != gneiss::editor::runtime_property_edit_state::applied ||
      state->revision != 2U ||
      edits.prepare(key, 2U, {true}, now, command) != gneiss::result::success) {
    return false;
  }
  const gneiss::ipc_property_write_result rejected{.session_id = 7U,
                                                   .command_id = 2U,
                                                   .code = GNEISS_ERROR_INVALID_ARGUMENT,
                                                   .revision = 2U,
                                                   .message = "属性写入被拒绝",
                                                   .canonical_value = {}};
  if (edits.accept(rejected) != gneiss::result::success) {
    return false;
  }
  state = edits.find(key);
  return state != nullptr &&
         state->state == gneiss::editor::runtime_property_edit_state::rejected &&
         state->revision == 2U;
}

bool test_timeout_disconnect_and_session() {
  gneiss::editor::runtime_property_edits edits;
  edits.begin_session(8U);
  const auto now = gneiss::editor::runtime_property_edits::clock::now();
  const auto key = make_key();
  gneiss::ipc_property_write command;
  if (edits.prepare(key, 1U, {true}, now, command) != gneiss::result::success) {
    return false;
  }
  edits.expire(now + std::chrono::seconds(3), std::chrono::seconds(2));
  const auto* state = edits.find(key);
  if (state == nullptr || state->state != gneiss::editor::runtime_property_edit_state::timed_out ||
      edits.accept({.session_id = 8U,
                    .command_id = 1U,
                    .code = GNEISS_SUCCESS,
                    .revision = 1U,
                    .message = {},
                    .canonical_value = {}}) != gneiss::result::not_found ||
      edits.prepare(key, 1U, {false}, now, command) != gneiss::result::success) {
    return false;
  }
  edits.disconnect();
  state = edits.find(key);
  if (state == nullptr ||
      state->state != gneiss::editor::runtime_property_edit_state::disconnected) {
    return false;
  }
  edits.begin_session(9U);
  return edits.find(key) == nullptr && edits.session_id() == 9U;
}

} // namespace

int main() {
  return test_result_and_single_pending() && test_timeout_disconnect_and_session() ? 0 : 1;
}
