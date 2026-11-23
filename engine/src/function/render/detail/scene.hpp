// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/function/render/commands.hpp>
#include "function/render/detail/frame_packet.hpp"
#include "function/render/detail/resource_identity.hpp"
#include <gneiss/engine/function/render/frame_request.hpp>
#include <map>
namespace gneiss::render {
/** 已校验并固定资源身份的内部命令；只由 Service 准备。 */
struct prepared_scene_command {
  command input;
  detail::scene_resource_pin resources;
  math::matrix4 normal{1.F};
};
/** 消费侧持久场景，不依赖 World、RID 表或执行器。所有访问限定同一消费者。 */
class scene final {
public:
  void apply(std::vector<prepared_scene_command> commands);
  result<void> extract(const view&, target_description, detail::frame_packet&) const;

private:
  struct instance {
    mesh_instance_description description;
    detail::scene_resource_pin resources;
    math::matrix4 normal_matrix{1.F};
  };
  std::map<std::uint64_t, instance> instances_;
};
}
