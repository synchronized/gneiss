// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_SCENE_MIRROR_H_
#define GNEISS_APPS_EDITOR_RUNTIME_SCENE_MIRROR_H_

#include "runtime_scene_data.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace gneiss::editor {

/** Editor 主线程拥有的 Runtime 只读场景镜像。 */
class runtime_scene_mirror final {
public:
  [[nodiscard]] result apply(const runtime_scene_batch& batch) noexcept;
  void invalidate() noexcept;
  void reset() noexcept;

  [[nodiscard]] bool needs_full_snapshot() const noexcept { return needs_full_snapshot_; }
  [[nodiscard]] std::uint64_t session_id() const noexcept { return sequence_.session_id(); }
  [[nodiscard]] const std::vector<runtime_scene_node>& nodes() const noexcept { return nodes_; }

private:
  [[nodiscard]] result apply_complete(const runtime_scene_batch& batch) noexcept;
  void rebuild_nodes();

  runtime_scene_sequence_tracker sequence_;
  std::map<std::uint64_t, runtime_scene_node> by_id_;
  std::vector<runtime_scene_node> nodes_;
  bool needs_full_snapshot_ = true;
  runtime_scene_stamp pending_stamp_;
  bool pending_is_full_ = false;
  std::vector<std::optional<runtime_scene_batch>> pending_chunks_;
};

} // namespace gneiss::editor

#endif
