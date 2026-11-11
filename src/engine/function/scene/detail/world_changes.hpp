// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/type_id.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace gneiss::detail {
/** 变化只表示需要重新读取，不保存组件借用或资源所有权。 */
struct object_change {
  bool transform{};
  bool destroyed{};
  // true 表示本轮发生过移除；移除后重新添加也必须保留这个事实。
  std::map<type_id, bool> components;
};
/** 所属线程消费；成功处理后由消费者清空。不同消费者有独立记录。 */
struct world_change_inbox {
  std::map<entity_id, object_change> objects;
};
/** World 私有变化源，不依赖 Render；未订阅时不积累历史。 */
class world_changes final {
public:
  [[nodiscard]] std::shared_ptr<world_change_inbox> subscribe() {
    auto inbox = std::make_shared<world_change_inbox>();
    std::erase_if(observers_, [](const auto& observer) { return observer.expired(); });
    observers_.push_back(inbox);
    return inbox;
  }
  [[nodiscard]] bool observed() const noexcept {
    return std::any_of(observers_.begin(), observers_.end(),
                       [](const auto& observer) { return !observer.expired(); });
  }
  void transform(entity_id id) {
    publish(id, [](auto& change) { change.transform = true; });
  }
  void component(entity_id id, type_id type, bool removed = false) {
    publish(id, [&](auto& change) { change.components[type] |= removed; });
  }
  void destroyed(entity_id id) {
    publish(id, [](auto& change) {
      change.destroyed = true;
      change.components.clear();
    });
  }

private:
  template <class F> void publish(entity_id id, F&& update) {
    std::erase_if(observers_, [&](auto& observer) {
      if (auto inbox = observer.lock()) {
        update(inbox->objects[id]);
        return false;
      }
      return true;
    });
  }
  std::vector<std::weak_ptr<world_change_inbox>> observers_;
};
}
