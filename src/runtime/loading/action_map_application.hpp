// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/error.hpp"
#include "engine/function/resource/asset_request.hpp"
#include "engine/resource/input/action_map.hpp"
namespace gneiss::engine {
class input_service;
}
namespace gneiss::runtime {
enum class action_map_apply_state { idle, loading, applied, failed, cancelled };
/** 每个宿主一个待应用槽，所有方法均在输入服务所属线程调用。
 * 只接收资产层创建的请求，不访问 manager、不建缓存；后台不持有本对象或 InputService。
 * 开始新请求即取消旧订阅，仅最后一次请求可由 poll 应用。 */
class action_map_application final {
public:
  ~action_map_application();
  action_map_application() = default;
  action_map_application(const action_map_application&) = delete;
  action_map_application& operator=(const action_map_application&) = delete;
  [[nodiscard]] gneiss::engine::error
  begin(gneiss::engine::asset_request<gneiss::engine::action_map> request) noexcept;
  void cancel() noexcept;
  [[nodiscard]] gneiss::engine::error status() const noexcept { return result_; }
  /** 不推进调度器或等待 I/O；加载完成后最多替换一次。失败保留原映射与 action_id。 */
  [[nodiscard]] gneiss::engine::error poll(gneiss::engine::input_service& target) noexcept;
  [[nodiscard]] action_map_apply_state state() const noexcept { return state_; }

private:
  gneiss::engine::asset_request<gneiss::engine::action_map> request_;
  action_map_apply_state state_ = action_map_apply_state::idle;
  gneiss::engine::error result_ = gneiss::engine::error::invalid_state;
};
} // namespace gneiss::runtime
