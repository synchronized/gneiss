// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_CONSOLE_MODEL_H_
#define GNEISS_APPS_EDITOR_CONSOLE_MODEL_H_

#include <gneiss/app/runtime_log_protocol.h>
#include <gneiss/core/result.hpp>

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

struct console_entry final {
  console_entry_kind kind = console_entry_kind::raw;
  std::uint64_t session_id = 0U;
  app::runtime_log_record event;
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
  [[nodiscard]] result append_event(std::uint64_t session_id,
                                    app::runtime_log_record event) noexcept;
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
  std::uint64_t dropped_count_ = 0U;
  std::deque<console_entry> entries_;
};

} // namespace gneiss::editor

#endif
