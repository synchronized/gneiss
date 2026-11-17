// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/ipc_protocol_domains.hpp>
#include <gneiss/protocol/ipc_scene_protocol.hpp>

#include <cstdio>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/protocol/registration.hpp>
#include <source_location>
#include <stdexcept>

using namespace gneiss::host;

namespace {
using namespace gneiss;
void check(bool value, std::source_location location = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("场景协议契约失败，行=" + std::to_string(location.line()));
  }
}
void run() {
  const ipc_scene_request source{5U, 7U, "asset://scenes/main.scene.json"};
  auto request = protocol::encode(scene_load, source, 9U);
  check(bool(request));
  auto decoded = protocol::decode(scene_load, *request);
  check(decoded && decoded->session == source.session && decoded->revision == source.revision &&
        decoded->uri == source.uri);
  check(!protocol::encode(scene_load, source));
  check(!protocol::encode(scene_cancel, source, 9U));
  auto cancel = protocol::encode(scene_cancel, source);
  check(cancel && protocol::decode(scene_cancel, *cancel));
  check(!protocol::decode(scene_load, *cancel));
  auto invalid_source = source;
  invalid_source.uri = "asset://../escape";
  check(!protocol::encode(scene_load, invalid_source, 9U));
  for (const std::string_view malformed :
       {"{}", R"({"session":0,"revision":7,"uri":"asset://scene.json"})",
        R"({"session":5,"revision":7,"uri":"asset://../escape"})"}) {
    request->payload.assign(malformed.begin(), malformed.end());
    check(!protocol::decode(scene_load, *request));
  }
  for (unsigned index = 0; index <= static_cast<unsigned>(ipc_scene_phase::cancelled); ++index) {
    const auto phase = static_cast<ipc_scene_phase>(index);
    ipc_scene_progress progress{.source = source,
                                .phase = phase,
                                .completed = 512,
                                .total = 65536,
                                .can_cancel = !scene_phase_terminal(phase),
                                .message = "阶段信息"};
    auto event = protocol::encode(scene_progress, progress);
    check(bool(event));
    auto value = protocol::decode(scene_progress, *event);
    check(value && value->phase == phase && value->source.uri == source.uri && !value->budget);
    auto response = protocol::encode(scene_result, progress, 9U);
    check(bool(response) == scene_phase_terminal(phase));
    if (response) {
      check(bool(protocol::decode(scene_result, *response)));
      check(!protocol::decode(scene_progress, *response));
    }
    progress.budget = ipc_scene_budget{UINT64_MAX, 2, 3, 4, 5, true, false};
    event = protocol::encode(scene_progress, progress);
    check(bool(event));
    value = protocol::decode(scene_progress, *event);
    check(value && value->budget && value->budget->candidate_logical_bytes == UINT64_MAX &&
          value->budget->candidate_cpu_data_bytes == 2 &&
          value->budget->application_logical_bytes == 3 &&
          value->budget->application_cpu_data_bytes == 4 && value->budget->available_bytes == 5 &&
          value->budget->cleanup_complete && !value->budget->cleanup_pending);
    const std::string text(event->payload.begin(), event->payload.end());
    for (const auto field : {std::string_view{",\"cleanup_complete\":true"},
                             std::string_view{",\"cleanup_pending\":false"}}) {
      auto malformed = text;
      const auto offset = malformed.find(field);
      check(offset != std::string::npos);
      malformed.erase(offset, field.size());
      event->payload.assign(malformed.begin(), malformed.end());
      check(!protocol::decode(scene_progress, *event));
    }
    for (const auto field :
         {std::string_view{"cleanup_pending"}, std::string_view{"cleanup_complete"}}) {
      for (const auto replacement : {"0", "null", "\"true\""}) {
        auto malformed = text;
        const auto marker = std::string("\"") + std::string(field) + "\":";
        const auto start = malformed.find(marker);
        check(start != std::string::npos);
        malformed.replace(start + marker.size(), field == "cleanup_pending" ? 5 : 4, replacement);
        event->payload.assign(malformed.begin(), malformed.end());
        check(!protocol::decode(scene_progress, *event));
      }
    }
    for (const auto replacement : {"-1", "1.5", "null", "18446744073709551616"}) {
      auto malformed = text;
      malformed.replace(malformed.find("18446744073709551615"), 20, replacement);
      event->payload.assign(malformed.begin(), malformed.end());
      check(!protocol::decode(scene_progress, *event));
    }
    progress.budget->cleanup_pending = true;
    check(!protocol::encode(scene_progress, progress));
    progress.budget.reset();
    progress.total = 65537;
    check(!protocol::encode(scene_progress, progress));
  }
  ipc_scene_progress draining{.source = source, .phase = ipc_scene_phase::cancelled, .message = {}};
  draining.budget.emplace();
  draining.budget->cleanup_pending = true;
  auto cleaned = draining;
  cleaned.budget->cleanup_pending = false;
  cleaned.budget->cleanup_complete = true;
  for (unsigned mismatch = 0; mismatch < 4; ++mismatch) {
    auto stale = cleaned;
    if (mismatch == 0)
      ++stale.source.session;
    if (mismatch == 1)
      ++stale.source.revision;
    if (mismatch == 2)
      stale.source.uri += ".other";
    if (mismatch == 3)
      stale.phase = ipc_scene_phase::applied;
    check(!apply_scene_cleanup_update(draining, stale));
  }
  check(apply_scene_cleanup_update(draining, cleaned));
  check(draining.phase == ipc_scene_phase::cancelled && draining.budget->cleanup_complete);
  check(!apply_scene_cleanup_update(draining, cleaned));
  draining.can_cancel = true;
  check(!protocol::encode(scene_progress, draining));
  request->payload.assign(ipc_scene_max_payload_size + 1, 0);
  check(!protocol::decode(scene_load, *request));
}
}
int main() try {
  check(gneiss::initialize_meta(gneiss::protocol::register_types).ok());
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
