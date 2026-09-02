// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_SCENE_MIRROR_H_
#define GNEISS_APPS_EDITOR_RUNTIME_SCENE_MIRROR_H_

#include "ipc_inspection_protocol.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace gneiss::editor {

/** Editor 主线程拥有的 Runtime 只读场景镜像。 */
class runtime_scene_mirror final {
public:
  [[nodiscard]] result apply(const ipc_inspection_batch& batch) noexcept;
  void invalidate() noexcept;
  void reset() noexcept;

  [[nodiscard]] bool needs_full_snapshot() const noexcept { return needs_full_snapshot_; }
  [[nodiscard]] std::uint64_t session_id() const noexcept { return sequence_.session_id(); }
  [[nodiscard]] const std::vector<ipc_inspection_node>& nodes() const noexcept { return nodes_; }

private:
  [[nodiscard]] result apply_complete(const ipc_inspection_batch& batch) noexcept;
  void rebuild_nodes();

  ipc_inspection_sequence_tracker sequence_;
  std::map<std::uint64_t, ipc_inspection_node> by_id_;
  std::vector<ipc_inspection_node> nodes_;
  bool needs_full_snapshot_ = true;
  ipc_inspection_stamp pending_stamp_;
  bool pending_is_full_ = false;
  std::vector<std::optional<ipc_inspection_batch>> pending_chunks_;
};

} // namespace gneiss::editor

#endif
