// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/rid_table.hpp"
#include "engine/function/render/commands.hpp"
#include "engine/function/render/detail/frame_packet.hpp"
#include "engine/function/render/detail/resource_identity.hpp"
#include "engine/function/render/frame_request.hpp"
#include <functional>
#include <map>
#include <set>
namespace gneiss::render::detail {
class render_scene_state final {
public:
  using pin_callback = std::function<result<scene_resource_pin>(const mesh_instance_description&)>;
  using dispatch_callback = std::function<result<void>(std::function<void()>)>;
  /** dispatch 必须可靠、有序且执行一次；失败不得执行或保留任务。空回调同步消费。
   * 异步模式下销毁前调用方须停止提交并排空消费者。 */
  explicit render_scene_state(std::uint16_t domain, pin_callback pin,
                              dispatch_callback dispatch = {})
      : scenes_(domain), pin_(std::move(pin)), dispatch_(std::move(dispatch)) {}
  result<rid> create_scene();
  result<void> submit(render::scene_commands&& commands);
  result<void> destroy_scene(rid scene);
  /** 同步消费适配；异步模式须改用 prepare_capture 并有序调度，不可并发直读。 */
  result<frame_packet> capture(const render::frame_request& request) const;
  /** 接受侧校验视图并绑定场景内容；返回任务须与场景命令在同一消费者按序调用。
   * 不捕获接受侧 this 或 RID 表，先准备的任务可在外部场景句柄释放后执行。 */
  using capture_operation = std::function<result<frame_packet>()>;
  result<capture_operation> prepare_capture(const render::frame_request& request,
                                            std::vector<target_description> sizes = {}) const;
  void close() noexcept;
  struct instance {
    mesh_instance_description description;
    scene_resource_pin resources;
    math::matrix4 normal_matrix{1.F};
  };
  /** 同步消费模式查询；异步模式仅在消费者排空后借用，不能并发读取。 */
  const instance* find(rid scene, render::instance_key key) const noexcept;

private:
  struct scene_content {
    std::map<std::uint64_t, instance> instances;
  };
  static result<frame_packet>
  capture_content(const render::frame_request& request,
                  const std::vector<std::shared_ptr<scene_content>>& contents,
                  const std::vector<target_description>& sizes);
  struct scene {
    // 接受侧只管理身份，不复制消费者的变换、可见性和资源状态。
    std::set<std::uint64_t> accepted;
    std::uint64_t last_key{};
    std::shared_ptr<scene_content> content = std::make_shared<scene_content>();
  };
  result<void> dispatch(std::function<void()> operation);
  rid_table<scene> scenes_;
  pin_callback pin_;
  dispatch_callback dispatch_;
  bool closed_{};
};
}
