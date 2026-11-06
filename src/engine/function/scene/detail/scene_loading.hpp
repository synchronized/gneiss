// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/scene_preparation.hpp"
namespace gneiss::engine::detail {
/** 所属线程推进普通资源请求；销毁即放弃本请求的观察权。
 * 宿主须先分发文件完成并推进资产管理器；本对象不驱动文件或任务队列。 */
class scene_description_request final {
public:
  GNEISS_API scene_description_request(asset_manager&, std::string uri, bool prefab,
                                       scene_prepare_limits limits = {}, bool reload = true);
  GNEISS_API ~scene_description_request();
  GNEISS_API error advance(scene_preparation_inputs&, scene_diagnostic&);

private:
  struct state;
  std::unique_ptr<state> state_;
};
/** 场景侧依赖收集和实例化数据准备；管理器只调度，不创建 World 对象。 */
class scene_preparation_operation final {
public:
  GNEISS_API scene_preparation_operation(asset_manager&, std::string uri, bool prefab, bool reload);
  GNEISS_API result<std::shared_ptr<const scene_instantiation_data>, scene_diagnostic> advance();
  const asset_load_metrics& metrics() const noexcept { return metrics_; }
  GNEISS_API void cancel() noexcept;

private:
  std::unique_ptr<scene_description_request> descriptions_;
  scene_preparation_inputs inputs_;
  std::shared_ptr<scene_instantiation_data> prepared_{std::make_shared<scene_instantiation_data>()};
  scene_diagnostic diagnostic_;
  std::string prefab_uri_;
  error status_{error::invalid_state};
  bool completed_{};
  asset_load_metrics metrics_;
};
}
