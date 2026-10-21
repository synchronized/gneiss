// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/resource/types/action_map.hpp"
#include <memory>
namespace gneiss::engine {
/** 单次资源订阅；不推进队列、不应用到 InputService。
 * 所属线程调用；成功仅转移一次映射副本，失败不改输出。销毁取消本订阅。
 * 请求须先于所属 asset_manager 销毁。 */
class action_map_request {
public:
  virtual ~action_map_request() = default;
  virtual result take(asset_internal::action_map& output) noexcept = 0;
  virtual void cancel() noexcept = 0;
};
}
