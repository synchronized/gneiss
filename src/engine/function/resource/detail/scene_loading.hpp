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
}
