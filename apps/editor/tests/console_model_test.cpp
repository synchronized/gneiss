// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "console_model.h"

#include <utility>

int main() {
  gneiss::editor::console_model model(2U);
  const auto first_session = model.begin_session();
  gneiss::app::runtime_log_record event;
  event.source = "application";
  event.category = "runtime";
  event.message = "first";
  if (first_session == 0U ||
      model.append_event(first_session, std::move(event)) != gneiss::result::success ||
      model.append_raw(first_session, "raw") != gneiss::result::success) {
    return 1;
  }
  const auto second_session = model.begin_session();
  if (second_session == first_session ||
      model.append_raw(second_session, "truncated", true) != gneiss::result::success ||
      model.entries().size() != 2U || model.dropped_count() != 1U ||
      model.entries().front().session_id != first_session ||
      model.entries().front().kind != gneiss::editor::console_entry_kind::raw ||
      model.entries().back().session_id != second_session ||
      !model.entries().back().was_truncated) {
    return 2;
  }
  model.clear();
  return model.entries().empty() && model.dropped_count() == 0U &&
                 model.current_session_id() == second_session
             ? 0
             : 3;
}
