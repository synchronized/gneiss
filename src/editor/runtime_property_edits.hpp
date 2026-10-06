// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_PROPERTY_EDITS_H_
#define GNEISS_APPS_EDITOR_RUNTIME_PROPERTY_EDITS_H_

#include "runtime_object_id.hpp"

#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/reflection.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>

namespace gneiss::editor {

/** 模型拥有的属性值；不借用协议缓冲区，不含传输版本。 */
struct runtime_property_value final {
  std::variant<std::monostate, bool, std::int64_t, std::uint64_t, float, double, std::string,
               std::array<std::uint8_t, 16>, std::array<float, 3>, std::array<float, 4>>
      payload;
};

struct runtime_property_write final {
  std::uint64_t session_id = 0U;
  std::uint64_t command_id = 0U;
  runtime_object_id object;
  gneiss_type_id type_id{};
  gneiss_field_id field_id = GNEISS_NULL_FIELD_ID;
  std::uint64_t expected_revision = 0U;
  runtime_property_value value;
};

struct runtime_property_write_result final {
  std::uint64_t session_id = 0U;
  std::uint64_t command_id = 0U;
  std::int32_t code = 0;
  std::uint64_t revision = 0U;
  std::string message;
  runtime_property_value canonical_value;
};

struct runtime_property_key final {
  runtime_object_id object;
  std::array<std::uint8_t, 16> type_id{};
  gneiss_field_id field_id = GNEISS_NULL_FIELD_ID;

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
  std::int32_t code = GNEISS_SUCCESS;
  std::string message;
  runtime_property_value canonical_value;
};

/** Editor 主线程拥有的 Runtime 属性写入状态模型。 */
class runtime_property_edits final {
public:
  using clock = std::chrono::steady_clock;

  void begin_session(std::uint64_t session_id) noexcept;
  void disconnect() noexcept;
  [[nodiscard]] result prepare(runtime_property_key key, std::uint64_t expected_revision,
                               runtime_property_value value, clock::time_point now,
                               runtime_property_write& output) noexcept;
  [[nodiscard]] result accept(runtime_property_write_result response) noexcept;
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
