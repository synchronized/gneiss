// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "render/render_asset_loader.h"
#include "scene/prefab_description.h"

#include <functional>
#include <map>

namespace gneiss::scene_internal {

struct scene_prepare_limits {
  std::size_t source_bytes{16U * 1024U * 1024U};
  std::size_t nodes{65536U};
  std::size_t dependencies{65536U};
  std::size_t hierarchy_depth{1024U};
};

struct prepared_prefab_description {
  prefab_description description;
  std::size_t maximum_depth{};
  std::vector<std::size_t> parent_first;
};

/** 工作者自有的不可变准备结果；不含 World、缓存租约或后端资源。
 * Prefab v1 不支持嵌套实例；渲染依赖的材质/纹理闭包由资产准备阶段解析。
 * source_bytes 只计原始描述；解析后的字符串与索引另占内存。 */
struct prepared_scene_description {
  scene_description description;
  std::vector<std::size_t> parent_first;
  std::map<std::string, prepared_prefab_description> prefabs;
  std::vector<render_internal::render_asset_reload> assets;
  std::size_t instance_nodes{};
  std::size_t source_bytes{};
};

/** 只读、有界 CPU 准备。支持在文件与节点边界取消，单次 JSON 解析不可抢占。
 * 完成前复读 Scene/Prefab 原始字节以检测变化；不承诺外部文件系统事务隔离。
 * 失败清空输出；取消返回 INVALID_STATE，由协调者区分取消与失败终态。 */
[[nodiscard]] gneiss_result
prepare_scene_description(const asset_internal::virtual_file_system& files, std::string_view uri,
                          prepared_scene_description& output, scene_diagnostic& diagnostic,
                          const std::function<bool()>& cancelled,
                          scene_prepare_limits limits = {}) noexcept;

} // namespace gneiss::scene_internal
