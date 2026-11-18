// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_PROPERTY_EDITS_H_
#define GNEISS_APPS_EDITOR_RUNTIME_PROPERTY_EDITS_H_

#include "inspection/runtime_object_id.hpp"

#include <gneiss/engine/core/data/value.hpp>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/error.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>

namespace gneiss::editor {

struct runtime_property_write final {
  std::uint64_t session_id = 0U;
  std::uint64_t command_id = 0U;
  runtime_object_id object;
  gneiss::guid type_id{};
  std::uint32_t field_id = 0U;
  std::uint64_t expected_revision = 0U;
  gneiss::value value;
};

struct runtime_property_write_result final {
  std::uint64_t session_id = 0U;
  std::uint64_t command_id = 0U;
  std::int32_t code = 0;
  std::uint64_t revision = 0U;
  std::string message;
  gneiss::value canonical_value;
};

struct runtime_property_key final {
  runtime_object_id object;
  std::array<std::uint8_t, 16> type_id{};
  std::uint32_t field_id = 0U;

  [[nodiscard]] bool operator<(const runtime_property_key& other) const noexcept {
    return std::tie(object.value, object.generation, type_id, field_id) <
           std::tie(other.object.value, other.object.generation, other.type_id, other.field_id);
  }
};

enum class runtime_property_edit_state : std::uint8_t {
  pending,
  applied,
  rejected,
  timed_out,
  disconnected,
};

struct runtime_property_edit final {
  runtime_property_edit_state state = runtime_property_edit_state::pending;
  std::uint64_t command_id = 0U;
  std::uint64_t revision = 0U;
  std::int32_t code = gneiss::error::success.code();
  std::string message;
  gneiss::value canonical_value;
};

/** Editor 主线程拥有的 Runtime 属性写入状态模型。 */
class runtime_property_edits final {
public:
  using clock = std::chrono::steady_clock;

  void begin_session(std::uint64_t session_id) noexcept;
  void disconnect() noexcept;
  [[nodiscard]] gneiss::error prepare(runtime_property_key key, std::uint64_t expected_revision,
                                      gneiss::value value, clock::time_point now,
                                      runtime_property_write& output);
  [[nodiscard]] gneiss::error accept(runtime_property_write_result response);
  void expire(clock::time_point now, std::chrono::milliseconds timeout) noexcept;
  [[nodiscard]] const runtime_property_edit* find(const runtime_property_key& key) const noexcept;
  [[nodiscard]] std::uint64_t session_id() const noexcept { return session_id_; }

private:
  struct pending_edit final {
    runtime_property_key key;
    clock::time_point sent_at;
  };

  std::uint64_t session_id_ = 0U;
  std::uint64_t next_command_id_ = 1U;
  std::map<runtime_property_key, runtime_property_edit> edits_;
  std::map<std::uint64_t, pending_edit> pending_;
};

} // namespace gneiss::editor

#endif
