// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/events/signal.hpp"
#include "engine/function/scene/object_change.hpp"
namespace gneiss::detail {
/** World 集中合并，宿主在逻辑修改结束后发布；回调不得修改 World。 */
class world_changes final {
public:
  auto changed() const { return changed_.sink(); }
  bool observed() const { return !changed_.empty(); }
  void transform(entity_id id) {
    if (observed() && !pending_[id].destroyed)
      pending_[id].transform = true;
  }
  void component(entity_id id, type_id type, bool removed = false) {
    if (observed() && !pending_[id].destroyed)
      pending_[id].components[type] |= removed;
  }
  void destroyed(entity_id id) {
    if (observed())
      pending_[id] = {.destroyed = true, .components = {}};
  }
  void flush() {
    if (pending_.empty())
      return;
    changed_.publish(pending_);
    pending_.clear();
  }

private:
  object_changes pending_;
  events::signal<void(const object_changes&)> changed_;
};
}
