// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/asset/render_resource.hpp"
#include "engine/function/scene/scene_application.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/resource/scene.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
namespace gneiss::runtime {
enum class scene_load_phase : std::uint8_t {
  preparing,
  assets,
  instantiating,
  ready,
  applied,
  failed,
  cancelled
};
struct scene_load_progress {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  scene_load_phase phase{scene_load_phase::preparing};
  std::size_t completed{};
  std::size_t total{};
  /** 去重后的候选逻辑容量；纹理释放 CPU 负载后仍计入所选负载，约束 2 GiB 上限。 */
  std::size_t resident_bytes{};
  bool can_cancel{};
  /** 候选已提交 CPU 数组字节与纹理逻辑负载；不是 RSS 或驱动显存。 */
  std::size_t cpu_data_bytes{};
  std::size_t texture_payload_bytes{};
  /** Application 资源账本快照；不含后台临时分配或驱动额外占用。 */
  std::uint64_t application_logical_bytes{};
  std::uint64_t application_cpu_data_bytes{};
  std::uint64_t available_bytes{};
  /** 失败/取消后请求容器与 CPU 资源引用已释放；不表示活动场景或旧帧占用归零。 */
  bool cleanup_complete{};
  /** 取消已确认，但 CPU 任务与候选清理仍在推进；此时不得重试。 */
  bool cleanup_pending{};
};
/** 调用方持有的来源重载基线；不拥有 World，不能跨 World 使用。
 * 仅所属线程读写；未保留此状态的场景不支持来源重载。 */
struct scene_reload_state {
  engine::world* owner{};
  scene_instance_id instance;
  engine::ref<const engine::scene_content> content;
  std::map<engine::object_address, engine::scene_object_association> objects;
  bool synchronized = true;
};
struct scene_load_completion {
  scene_load_progress progress;
  gneiss::engine::error result{gneiss::engine::error::internal};
  std::string message;
  gneiss::scene_instance_id scene{};
  /** 本次实例化的不可变输入与实际资源引用；Runtime 为来源热更新保留固定内容与地址关联。 */
  std::vector<engine::scene_object_binding> objects;
  engine::ref<const engine::scene> source;
  engine::ref<const engine::scene_content> content;
  engine::ref<scene_reload_state> reload_state;
  std::map<std::string, engine::render_resource_ref, std::less<>> bindings;
  double prepare_ms{};
  double asset_prepare_ms{};
  double maximum_advance_ms{};
  double activation_ms{};
  double cleanup_ms{};
};

/** 实例结构重载的终态；外层 result 表示轮询操作是否有效。 */
struct scene_reload_completion {
  engine::error result{engine::error::success};
  std::vector<engine::scene_object_binding> objects;
};

} // namespace gneiss::runtime
