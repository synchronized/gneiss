// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_request.hpp"
namespace gneiss::engine::detail {
/** 记录持有当前操作；请求只共享观察状态，执行队列不承担身份查询。 */
struct load_operation {
  virtual ~load_operation() = default;
  std::shared_ptr<asset_state> observer{std::make_shared<asset_state>()};
};
}
