// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/asset_types.hpp"
#include "engine/resource/scene/prefab_description.hpp"

#include <functional>
#include <map>

namespace gneiss::engine {

struct scene_prepare_limits {
  // 累计保留的作者 JSON 字节数；单文件读取上限由 codec 规定。
  std::size_t source_bytes{16U * 1024U * 1024U};
  std::size_t nodes{65536U};
  std::size_t dependencies{65536U};
  std::size_t hierarchy_depth{1024U};
};

struct prepared_prefab_description {
  asset_ref<const prefab_description> resource;
  std::size_t maximum_depth{};
  std::vector<std::size_t> parent_first;
};

/** 工作者自有的准备结果；持有不可变 Prefab 引用，不含 World 或后端资源。
 * Prefab v1 不支持嵌套实例；渲染依赖的材质/纹理闭包由资产准备阶段解析。
 * source_bytes 只计解析后保留的作者 JSON；解析后的字符串与索引另占内存。 */
struct prepared_scene_description {
  scene_description description;
  std::vector<std::size_t> parent_first;
  std::map<std::string, prepared_prefab_description> prefabs;
  std::vector<asset_source> assets;
  std::size_t instance_nodes{};
  // 累计保留的作者 JSON 字节数；单文件读取上限由 codec 规定。
  std::size_t source_bytes{};
};

/** 所属线程取得的不可变输入；工作线程不读取文件或访问 manager。 */
struct scene_description_inputs {
  asset_ref<const scene_description> scene;
  std::map<std::string, asset_ref<const prefab_description>> prefabs;
};
/** 所属线程推进普通资源请求；销毁即放弃本请求的观察权。
 * 宿主须先分发文件完成并推进资产管理器；本对象不驱动文件或任务队列。 */
class GNEISS_API scene_description_request final {
public:
  scene_description_request(asset_manager&, std::string uri, bool prefab,
                            scene_prepare_limits limits = {});
  ~scene_description_request();
  result advance(scene_description_inputs&, scene_diagnostic&);

private:
  struct state;
  std::unique_ptr<state> state_;
};
/** 纯 CPU 排序和依赖收集；prefab_uri 非空表示仅准备一个 Prefab。 */
[[nodiscard]] GNEISS_API result prepare_loaded_scene(const scene_description_inputs&,
                                                     std::string_view prefab_uri,
                                                     prepared_scene_description&, scene_diagnostic&,
                                                     const std::function<bool()>& cancelled,
                                                     scene_prepare_limits limits = {}) noexcept;
} // namespace gneiss::engine
