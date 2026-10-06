// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_CONSOLE_MODEL_H_
#define GNEISS_APPS_EDITOR_CONSOLE_MODEL_H_

#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/log.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::editor {

enum class console_entry_kind : std::uint8_t {
  structured,
  raw,
};

/** 控制台持有的日志值；不含传输版本或协议对象，字符串由该值拥有。 */
struct console_event final {
  std::uint32_t severity = GNEISS_LOG_INFO;
  std::uint64_t sequence = 0U;
  std::uint64_t timestamp_ns = 0U;
  std::uint64_t thread_id = 0U;
  std::string source;
  std::string category;
  std::string message;
  gneiss_result operation = GNEISS_SUCCESS;
};

struct console_entry final {
  std::uint64_t id = 0U;
  console_entry_kind kind = console_entry_kind::raw;
  std::uint64_t session_id = 0U;
  console_event event;
  std::string raw_text;
  bool was_truncated = false;
};

struct console_filter final {
  std::uint32_t severity_mask = UINT32_C(0x7E);
  bool include_structured = true;
  bool include_raw = true;
  bool current_session_only = false;
  std::string source;
  std::string category;
  std::string search;
};

class console_model final {
public:
  explicit console_model(std::size_t capacity = 4096U) noexcept;

  /** 开始新 Runtime 会话；已有记录保留原会话标识。 */
  [[nodiscard]] std::uint64_t begin_session() noexcept;
  [[nodiscard]] result append_event(std::uint64_t session_id, console_event event) noexcept;
  [[nodiscard]] result append_raw(std::uint64_t session_id, std::string_view text,
                                  bool was_truncated = false) noexcept;
  void clear() noexcept;

  [[nodiscard]] std::uint64_t current_session_id() const noexcept;
  [[nodiscard]] std::uint64_t dropped_count() const noexcept;
  [[nodiscard]] const std::deque<console_entry>& entries() const noexcept;
  [[nodiscard]] bool matches(const console_entry& entry,
                             const console_filter& filter) const noexcept;
  [[nodiscard]] result visible_indices(const console_filter& filter,
                                       std::vector<std::size_t>& output) const noexcept;

private:
  void make_room() noexcept;

  std::size_t capacity_;
  std::uint64_t current_session_id_ = 0U;
  std::uint64_t next_entry_id_ = 1U;
  std::uint64_t dropped_count_ = 0U;
  std::deque<console_entry> entries_;
};

} // namespace gneiss::editor

#endif
