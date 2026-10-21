// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "action_map_request.hpp"
#include "engine/asset/caors_action_map.hpp"
namespace gneiss::engine {
/** 仅资产实现与接入测试使用；不进入 Application 或公共 SDK。 */
std::unique_ptr<action_map_request>
make_action_map_request(caors::resources::load_request<asset_internal::action_map_asset> request);
}
