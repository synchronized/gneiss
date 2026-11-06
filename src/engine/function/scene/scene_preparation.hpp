// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/error.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/asset/render_resource.hpp"
#include "engine/resource/prefab.hpp"
#include "engine/resource/scene.hpp"
#include "engine/resource/scene/prefab_description.hpp"

#include <functional>
#include <map>

namespace gneiss::engine {

/** 场景侧异步准备入口；不创建 World，不等待视觉内容。宿主继续推进资产 I/O 与任务。
 * 迁移期由资产管理器私有队列调度，取消只放弃本请求的观察权。 */
GNEISS_API result<asset_request<scene_instantiation_data>>
prepare_scene(asset_manager&, std::string_view uri, bool reload = false) noexcept;
GNEISS_API result<asset_request<scene_instantiation_data>>
prepare_prefab(asset_manager&, std::string_view uri, bool reload = false) noexcept;

struct scene_prepare_limits {
  // 累计读取的源文件字节数；单文件读取上限由 codec 规定。
  std::size_t source_bytes{16U * 1024U * 1024U};
  std::size_t nodes{65536U};
  std::size_t dependencies{65536U};
  std::size_t hierarchy_depth{1024U};
};

struct prefab_instantiation_data {
  asset_ref<const prefab_description> resource;
  ref<const prefab> source_asset{};
  std::size_t maximum_depth{};
  std::vector<std::size_t> parent_first;
};

/** 工作者自有的准备结果；持有不可变 Prefab 引用，不含 World 或后端资源。
 * Prefab v1 不支持嵌套实例；渲染依赖的材质/纹理闭包由资产准备阶段解析。
 * source_bytes 是加载统计，不代表解析后内存用量。 */
struct scene_instantiation_data {
  scene_description description;
  ref<const scene> source_asset{};
  std::vector<std::size_t> parent_first;
  std::map<std::string, prefab_instantiation_data> prefabs;
  std::vector<render_resource_source> assets;
  std::size_t instance_nodes{};
  // 累计读取的源文件字节数；单文件读取上限由 codec 规定。
  std::size_t source_bytes{};
};

/** 所属线程取得的不可变输入；工作线程不读取文件或访问 manager。 */
struct scene_preparation_inputs {
  std::size_t source_bytes{}; // 文件加载请求统计；纯内存构造的输入为零。
  asset_ref<const scene_description> scene;
  std::map<std::string, asset_ref<const prefab_description>> prefabs;
  // 身份保活与不可变快照分开；纯内存编辑输入允许没有文件资产。
  ref<const engine::scene> source_asset{};
  std::map<std::string, ref<const prefab>> prefab_assets{};
};
/** 纯 CPU 排序和依赖收集；prefab_uri 非空表示仅准备一个 Prefab。 */
[[nodiscard]] GNEISS_API error prepare_loaded_scene(const scene_preparation_inputs&,
                                                    std::string_view prefab_uri,
                                                    scene_instantiation_data&, scene_diagnostic&,
                                                    const std::function<bool()>& cancelled,
                                                    scene_prepare_limits limits = {}) noexcept;
} // namespace gneiss::engine
