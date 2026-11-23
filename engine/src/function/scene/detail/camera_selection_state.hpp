// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/scene/entity_id.hpp>
namespace gneiss::detail {
/** 渲染相机选择的借用身份；查询时校验实体及相机组件，不保活对象。 */
struct camera_selection_state {
  entity_id entity;
};
}
