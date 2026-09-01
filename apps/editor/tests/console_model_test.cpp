// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "console_model.h"

#include <utility>
#include <vector>

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

  gneiss::editor::console_filter filter;
  filter.current_session_only = true;
  filter.search = "truncated";
  std::vector<std::size_t> visible;
  if (model.visible_indices(filter, visible) != gneiss::result::success || visible.size() != 1U ||
      visible.front() != 1U) {
    return 4;
  }
  filter.source = "application";
  if (model.visible_indices(filter, visible) != gneiss::result::success || !visible.empty()) {
    return 5;
  }
  filter = {};
  filter.include_raw = false;
  filter.severity_mask = UINT32_C(1) << GNEISS_LOG_INFO;
  if (model.visible_indices(filter, visible) != gneiss::result::success || !visible.empty()) {
    return 6;
  }

  gneiss::editor::console_model structured_model;
  const auto structured_session = structured_model.begin_session();
  gneiss::app::runtime_log_record warning;
  warning.severity = GNEISS_LOG_WARNING;
  warning.source = "game";
  warning.category = "asset";
  warning.message = "missing texture";
  if (structured_model.append_event(structured_session, std::move(warning)) !=
      gneiss::result::success) {
    return 7;
  }
  filter = {};
  filter.include_raw = false;
  filter.severity_mask = UINT32_C(1) << GNEISS_LOG_WARNING;
  filter.source = "game";
  filter.category = "asset";
  filter.search = "texture";
  if (structured_model.visible_indices(filter, visible) != gneiss::result::success ||
      visible.size() != 1U) {
    return 8;
  }

  model.clear();
  return model.entries().empty() && model.dropped_count() == 0U &&
                 model.current_session_id() == second_session
             ? 0
             : 3;
}
