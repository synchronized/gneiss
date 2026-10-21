// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/platform/caors/document_loader.hpp"
#include "engine/resource/types/action_map.hpp"
#include <functional>

namespace gneiss::engine::platform::caors {
class file_system_adapter;
}
namespace gneiss::engine::resource {
struct action_map_asset final : ::caors::resource {
  using ::caors::resource::resource;
  action_map map;
};
/** 宿主推进；销毁前排空工作 scope，abandon，再排空完成队列及 loop。 */
using action_map_loader = engine::platform::caors::document_loader<action_map_asset>;
/** verify 在 worker 中、解析完成且发布缓存之前调用；参数借用至回调返回。
 * URI 是来源后端路径，回调须支持后台调用，不得访问 InputService。失败不替换缓存。 */
std::unique_ptr<action_map_loader> make_action_map_loader(
    engine::platform::caors::file_system_adapter& files,
    std::function<result(std::string_view, std::span<const std::byte>)> verify = {});

} // namespace gneiss::engine::resource
