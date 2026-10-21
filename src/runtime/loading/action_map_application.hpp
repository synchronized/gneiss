// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/resource/action_map_request.hpp"
namespace gneiss::input_internal {
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
  [[nodiscard]] result
  begin(std::unique_ptr<engine::resource::action_map_request> request) noexcept;
  void cancel() noexcept;
  [[nodiscard]] result status() const noexcept { return result_; }
  /** 不推进调度器或等待 I/O；加载完成后最多替换一次。失败保留原映射与 action_id。 */
  [[nodiscard]] result poll(input_internal::input_service& target) noexcept;
  [[nodiscard]] action_map_apply_state state() const noexcept { return state_; }

private:
  std::unique_ptr<engine::resource::action_map_request> request_;
  action_map_apply_state state_ = action_map_apply_state::idle;
  result result_ = result::invalid_state;
};
} // namespace gneiss::runtime
