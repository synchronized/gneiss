// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "console_log_buffer.hpp"
#include "console_model.hpp"
#include "engine/core/error.hpp"
#include "engine/core/logging/service.hpp"
#include <thread>

#include <utility>
#include <vector>

namespace {
struct observer {
  unsigned count{};
  void changed() { ++count; }
};
bool check_local_logs() {
  gneiss::host::console_log_buffer buffer;
  gneiss::editor::console_model model;
  observer listener;
  gneiss::events::scoped_connection subscription =
      model.changed().connect<&observer::changed>(listener);
  gneiss::logging::service logs;
  auto connection = logs.attach(buffer.make_sink());
  auto log = logs.get_logger("editor");
  std::thread worker([&] {
    log.write({.context = 42,
               .severity = gneiss::logging::level::warning,
               .category = "test",
               .message = "local",
               .result = gneiss::error::io});
  });
  worker.join();
  logs.flush();
  if (!model.entries().empty() || listener.count != 0)
    return false;
  buffer.drain(model);
  if (listener.count != 1 || model.entries().size() != 1 ||
      model.entries().front().session_id != 0 || model.entries().front().event.context != 42 ||
      model.entries().front().event.operation != gneiss::error::io)
    return false;
  connection.disconnect();
  log.info("disconnected");
  logs.close();
  buffer.drain(model);
  if (model.entries().size() != 1)
    return false;
  subscription.disconnect();
  model.clear();
  if (listener.count != 1)
    return false;
  auto sink = buffer.make_sink();
  for (std::size_t i = 0; i < buffer.capacity + 2; ++i)
    if (sink->write({.message = {.message = "bounded"}}).failed())
      return false;
  buffer.drain(model);
  return model.entries().size() == buffer.capacity + 1 &&
         model.entries().front().event.message.find("2") != std::string::npos;
}
}
int main() {
  if (!check_local_logs())
    return 20;
  gneiss::editor::console_model model(2U);
  const auto first_session = model.begin_session();
  gneiss::editor::console_event event;
  event.source = "application";
  event.category = "runtime";
  event.message = "first";
  if (first_session == 0U ||
      model.append_event(first_session, std::move(event)) != gneiss::error::success ||
      model.append_raw(first_session, "raw") != gneiss::error::success) {
    return 1;
  }
  const auto second_session = model.begin_session();
  if (second_session == first_session ||
      model.append_raw(second_session, "truncated", true) != gneiss::error::success ||
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
  if (model.visible_indices(filter, visible) != gneiss::error::success || visible.size() != 1U ||
      visible.front() != 1U) {
    return 4;
  }
  filter.source = "application";
  if (model.visible_indices(filter, visible) != gneiss::error::success || !visible.empty()) {
    return 5;
  }
  filter = {};
  filter.include_raw = false;
  filter.severity_mask = UINT32_C(1) << static_cast<std::uint32_t>(gneiss::logging::level::info);
  if (model.visible_indices(filter, visible) != gneiss::error::success || !visible.empty()) {
    return 6;
  }

  gneiss::editor::console_model structured_model;
  const auto structured_session = structured_model.begin_session();
  gneiss::editor::console_event warning;
  warning.severity = static_cast<std::uint32_t>(gneiss::logging::level::warning);
  warning.source = "game";
  warning.category = "asset";
  warning.message = "missing texture";
  if (structured_model.append_event(structured_session, std::move(warning)) !=
      gneiss::error::success) {
    return 7;
  }
  filter = {};
  filter.include_raw = false;
  filter.severity_mask = UINT32_C(1) << static_cast<std::uint32_t>(gneiss::logging::level::warning);
  filter.source = "game";
  filter.category = "asset";
  filter.search = "texture";
  if (structured_model.visible_indices(filter, visible) != gneiss::error::success ||
      visible.size() != 1U) {
    return 8;
  }

  gneiss::editor::console_model disabled(0U);
  if (disabled.append_event(disabled.begin_session(), {}) != gneiss::error::invalid_argument ||
      model.append_event(0U, {}) != gneiss::error::invalid_argument ||
      !disabled.entries().empty()) {
    return 9;
  }

  model.clear();
  return model.entries().empty() && model.dropped_count() == 0U &&
                 model.current_session_id() == second_session
             ? 0
             : 3;
}
