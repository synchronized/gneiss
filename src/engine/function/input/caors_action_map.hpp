// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "action_map.hpp"
#include <caors/assets.h>
#include <functional>

namespace gneiss::caors_internal {
class file_system_adapter;
}
namespace gneiss::input_internal {
class input_service;
struct action_map_asset final : caors::resource {
  using caors::resource::resource;
  action_map map;
};
/** 宿主推进；销毁前排空工作 scope，abandon，再排空完成队列及 loop。 */
class action_map_loader : public caors::resources::async_loader<action_map_asset> {
public:
  virtual void pump() = 0;
  virtual bool empty() = 0;
  virtual void abandon() = 0;
  virtual std::size_t started_count() const noexcept = 0;
};
/** verify 在 worker 中、解析完成且发布缓存之前调用；参数借用至回调返回。
 * URI 是来源后端路径，回调须支持后台调用，不得访问 InputService。失败不替换缓存。 */
std::unique_ptr<action_map_loader> make_action_map_loader(
    caors_internal::file_system_adapter& files,
    std::function<result(std::string_view, std::span<const std::byte>)> verify = {});

enum class action_map_apply_state { idle, loading, applied, failed, cancelled };
/** 每个宿主一个待应用槽，所有方法均在输入服务所属线程调用。
 * 不拥有 manager/InputService，不建缓存；后台回调不持有本对象或 InputService。
 * 开始新请求即取消旧订阅，仅最后一次请求可由 poll 应用。 */
class action_map_application final {
public:
  ~action_map_application();
  action_map_application() = default;
  action_map_application(const action_map_application&) = delete;
  action_map_application& operator=(const action_map_application&) = delete;
  [[nodiscard]] result begin(caors::assets::asset_manager& manager, caors::resource_guid guid,
                             bool reload = false) noexcept;
  void cancel() noexcept;
  /** 不推进调度器或等待 I/O；加载完成后最多替换一次。失败保留原映射与 action_id。 */
  [[nodiscard]] result poll(input_service& target) noexcept;
  [[nodiscard]] action_map_apply_state state() const noexcept { return state_; }

private:
  caors::resources::load_request<action_map_asset> request_;
  action_map_apply_state state_ = action_map_apply_state::idle;
  result result_ = result::invalid_state;
};
} // namespace gneiss::input_internal
