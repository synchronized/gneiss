// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/render/detail/executor.hpp"
#include "function/render/detail/scene.hpp"
#include <barrier>
#include <gneiss/engine/function/render/service.hpp>
#include <iostream>
#include <latch>
#include <limits>
#include <thread>
using namespace gneiss;
using namespace gneiss::render::detail;
#define CHECK(expression)                                                                          \
  do {                                                                                             \
    if (!(expression)) {                                                                           \
      std::cerr << __LINE__ << ": " #expression "\n";                                              \
      return false;                                                                                \
    }                                                                                              \
  } while (false)
bool commands() {
  render::service owner, foreign;
  auto group = owner.create_scene();
  auto mesh = owner.reserve_mesh();
  auto material = owner.reserve_material();
  CHECK(group && mesh && material);
  render::scene_commands batch{
      *group,
      {render::create_instance{{1}, {.mesh = *mesh, .material = *material}},
       render::set_transform{{1}, math::matrix4{1.F}}}};
  CHECK(!foreign.submit(std::move(batch)) && batch.entries.size() == 2);
  CHECK(owner.submit(std::move(batch)) && batch.entries.empty() && !batch.scene);
  batch = {*group, {render::create_instance{{1}, {.mesh = *mesh, .material = *material}}}};
  CHECK(!owner.submit(std::move(batch)));
  auto singular = math::matrix4{0.F};
  batch = {*group,
           {render::create_instance{{2}, {.mesh = *mesh, .material = *material}},
            render::set_transform{{2}, singular}}};
  CHECK(!owner.submit(std::move(batch)) && batch.entries.size() == 2);
  // 拒绝整批不得占用新身份，修正后可以重试。
  batch.entries.back() = render::set_transform{{2}, math::matrix4{1.F}};
  CHECK(owner.submit(std::move(batch)));
  batch = {*group, {render::destroy_instance{{2}}, render::set_transform{{2}, math::matrix4{1.F}}}};
  CHECK(!owner.submit(std::move(batch)));
  batch.entries.pop_back();
  CHECK(owner.submit(std::move(batch)));
  batch = {*group, {render::set_transform{{2}, math::matrix4{1.F}}}};
  CHECK(!owner.submit(std::move(batch)));
  auto invalid = math::matrix4{1.F};
  invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
  batch = {*group, {render::set_transform{{1}, invalid}}};
  CHECK(!owner.submit(std::move(batch)));
  // 未初始化 GPU 也能验证视图参数；有效请求才到设备就绪检查。
  render::frame_request request{.surface = {.width = 64, .height = 32},
                                .views = {{.scene = *group}}};
  request.views[0].projection_matrix = invalid;
  CHECK(owner.render(std::move(request)).error() == error::invalid_argument &&
        request.views.size() == 1);
  request.views[0].projection_matrix = math::matrix4{1.F};
  request.views.push_back(request.views[0]);
  CHECK(owner.render(std::move(request)).error() == error::invalid_argument);
  request.views.pop_back();
  CHECK(owner.render(std::move(request)).error() == error::not_ready);
  CHECK(owner.destroy_scene(*group) && !owner.destroy_scene(*group));
  CHECK(owner.render(std::move(request)).error() == error::invalid_handle);
  auto replacement = owner.create_scene();
  CHECK(replacement && *replacement != *group && !owner.submit(std::move(batch)));
  return true;
}
bool queued_scene_consumption() {
  std::vector<float> positions;
  threaded_render_executor executor;
  CHECK(executor
            .initialize([&](frame_packet& packet, render::execution_result&) {
              positions.push_back(packet.views.front().scene.instances.front().world_matrix[3][0]);
              return error::success;
            })
            .ok());
  std::latch entered{1}, release{1};
  bool released = false;
  struct unblock {
    std::latch& gate;
    bool& released;
    ~unblock() {
      if (!released)
        gate.count_down();
    }
  } guard{release, released};
  CHECK(executor.submit_command([&](const auto&) {
    entered.count_down();
    release.wait();
    return error::success;
  }));
  entered.wait();
  auto content = std::make_shared<render::scene>();
  auto mesh = std::make_shared<resource_identity<mesh_data>>();
  auto material = std::make_shared<resource_identity<material_data>>();
  std::weak_ptr<render::scene> lifetime = content;
  auto enqueue = [&](std::vector<render::prepared_scene_command> commands) {
    return executor.submit_command(
        [content, commands = std::move(commands)](const render_command_reporter&) mutable {
          content->apply(std::move(commands));
          return error::success;
        },
        render_command_policy::once);
  };
  render_frame_prepare capture = [content]() -> result<frame_packet> {
    frame_packet packet;
    auto status = content->extract({}, {64, 64}, packet);
    if (!status)
      return status.error();
    return packet;
  };
  CHECK(enqueue({{render::create_instance{{1}, {.mesh = 1, .material = 2}}, {mesh, material}}}));
  CHECK(executor.submit_frame({}, render::frame_policy::required, capture));
  auto matrix = math::matrix4{1.F};
  matrix[3][0] = 5;
  CHECK(enqueue({{render::set_transform{{1}, matrix}, {}, math::matrix4{1.F}}}));
  CHECK(executor.submit_frame({}, render::frame_policy::required, capture));
  content.reset();
  capture = {};
  CHECK(!lifetime.expired());
  released = true;
  release.count_down();
  CHECK(executor.flush().ok());
  executor.stop();
  CHECK(positions.size() == 2 && positions[0] == 0 && positions[1] == 5);
  CHECK(lifetime.expired());
  return true;
}
bool scene_extraction() {
  render::scene content;
  auto mesh = std::make_shared<resource_identity<mesh_data>>();
  auto material = std::make_shared<resource_identity<material_data>>();
  content.apply({{render::create_instance{{1}, {.mesh = 1, .material = 2}}, {mesh, material}}});
  frame_packet packet;
  CHECK(content.extract({}, {64, 32}, packet));
  CHECK(packet.views.size() == 1 && packet.views[0].scene.instances.size() == 1);
  content.apply({{render::set_visibility{{1}, {.layers = 2}}, {}}});
  packet = {};
  CHECK(content.extract({.layers = 1}, {64, 32}, packet) &&
        packet.views[0].scene.instances.empty());
  CHECK(content.extract({.layers = 2}, {128, 64}, packet) &&
        packet.views[1].scene.instances.size() == 1);
  CHECK(packet.views[1].width == 128);
  content.apply({{render::set_visibility{{1}, {.casts_shadows = false}}, {}}});
  packet = {};
  CHECK(content.extract({}, {64, 32}, packet));
  CHECK(packet.views[0].scene.instances.size() == 1 &&
        !packet.views[0].scene.instances[0].casts_shadows);
  packet = {};
  content.apply({{render::destroy_instance{{1}}, {}}});
  CHECK(content.extract({}, {64, 32}, packet) && packet.views[0].scene.instances.empty());
  return true;
}
bool service_lifetime() {
  render::scene_commands pending;
  {
    gneiss::render::service owner;
    gneiss::render::service foreign;
    auto scene = owner.create_scene();
    CHECK(scene);
    auto mesh = owner.reserve_mesh();
    auto material = owner.reserve_material();
    CHECK(mesh && material);
    pending = {*scene, {render::create_instance{{1}, {.mesh = *mesh, .material = *material}}}};
    CHECK(!foreign.submit(std::move(pending)));
    CHECK(owner.submit(std::move(pending))); // 身份有效即可绑定，无需等待内容。
    auto first_mesh = std::make_shared<mesh_data>();
    auto old_mesh = std::weak_ptr<const mesh_data>{first_mesh};
    CHECK(owner.replace_mesh(*mesh, first_mesh));
    first_mesh.reset();
    auto retained_frame = owner.share_mesh(*mesh);
    CHECK(owner.replace_mesh(*mesh, std::make_shared<mesh_data>()));
    CHECK(!old_mesh.expired());
    retained_frame.reset();
    CHECK(old_mesh.expired()); // 实例保活身份，不再永久固定首个 payload。

    auto texture = owner.reserve_texture();
    CHECK(texture);
    auto description = std::make_shared<material_data>();
    description->base_color_texture = *texture;
    CHECK(owner.replace_material(*material, description));
    auto pixels = std::make_shared<texture_data>();
    auto retained = std::weak_ptr<const texture_data>{pixels};
    CHECK(owner.replace_texture(*texture, pixels));
    pixels.reset();
    CHECK(owner.destroy_texture(*texture).ok());
    CHECK(!retained.expired());
    // 原有依赖可在外部 RID 释放后保留、重排；未知依赖失败且旧内容保持。
    auto reordered = std::make_shared<material_data>();
    reordered->normal_texture = *texture;
    CHECK(owner.replace_material(*material, reordered));
    auto invalid = std::make_shared<material_data>();
    invalid->base_color_texture = *mesh;
    CHECK(!owner.replace_material(*material, invalid));
    CHECK(owner.share_material(*material) == reordered && !retained.expired());
    // 无资源换绑的修改不重新查询已交还的外部 RID。
    CHECK(owner.destroy_mesh(*mesh).ok());
    CHECK(owner.destroy_material(*material).ok());
    CHECK(!retained.expired());
    pending = {*scene, {render::create_instance{{2}, {.mesh = *mesh, .material = *material}}}};
    CHECK(!owner.submit(std::move(pending))); // 已释放的 RID 不能产生新绑定。
    pending = {*scene, {render::set_visibility{{1}, {.visible = false}}}};
    CHECK(owner.submit(std::move(pending)));
    pending = {*scene, {render::destroy_instance{{1}}}};
    gneiss::render::device_stats stats{};
    CHECK(owner.shutdown(stats).ok());
    CHECK(!owner.is_valid());
    CHECK(!owner.create_scene() && !owner.reserve_mesh());
    CHECK(owner.mesh_status(*mesh) == error::invalid_handle);
    CHECK(owner.shutdown(stats).ok());
    CHECK(retained.expired());
    CHECK(!owner.submit(std::move(pending)));
  }
  CHECK(pending.entries.size() == 1); // 命令可晚于服务销毁，不访问服务内存。
  return true;
}
bool identity_versions() {
  auto mesh = std::make_shared<gneiss::render::detail::resource_identity<mesh_data>>();
  auto material = std::make_shared<gneiss::render::detail::resource_identity<material_data>>();
  auto texture = std::make_shared<gneiss::render::detail::resource_identity<texture_data>>();
  scene_resource_pin pin{mesh, material};
  auto capture = [&] { return pin.capture(); };
  CHECK(!capture().mesh && !capture().material);
  mesh->publish(std::make_shared<mesh_data>());
  auto first = std::make_shared<material_data>();
  first->base_color_texture = 3;
  material->publish(first, {texture});
  CHECK(capture().mesh && capture().material && !capture().textures[0]);
  texture->publish(std::make_shared<texture_data>());
  auto before = capture();
  auto old_texture = std::weak_ptr<const texture_data>{before.textures[0]};
  auto old_mesh = std::weak_ptr<const mesh_data>{before.mesh};
  mesh->publish(std::make_shared<mesh_data>());
  texture->publish(std::make_shared<texture_data>());
  auto after = capture();
  CHECK(after.mesh != before.mesh && after.textures[0] != before.textures[0]);
  CHECK(after.material == before.material); // 纹理内容刷新不重建材质。
  CHECK(!old_texture.expired() && !old_mesh.expired());
  material->publish(std::make_shared<material_data>());
  CHECK(!capture().textures[0] && before.textures[0] && after.textures[0]);
  before = {};
  CHECK(old_texture.expired() && old_mesh.expired());
  pin = {};
  CHECK(after.mesh && after.material && after.textures[0]); // 已捕获内容跨场景卸载存活。
  return true;
}
bool concurrent_material_publication() {
  auto material = std::make_shared<gneiss::render::detail::resource_identity<material_data>>();
  auto left = std::make_shared<material_data>();
  auto right = std::make_shared<material_data>();
  left->base_color_texture = 3;
  right->normal_texture = 4;
  auto left_texture = std::make_shared<gneiss::render::detail::resource_identity<texture_data>>();
  auto right_texture = std::make_shared<gneiss::render::detail::resource_identity<texture_data>>();
  auto left_pixels = std::make_shared<texture_data>();
  auto right_pixels = std::make_shared<texture_data>();
  left_texture->publish(left_pixels);
  right_texture->publish(right_pixels);
  gneiss::render::detail::resource_identity<material_data>::dependencies left_dependencies{
      left_texture};
  gneiss::render::detail::resource_identity<material_data>::dependencies right_dependencies{};
  right_dependencies[2] = right_texture;
  material->publish(left, left_dependencies);
  gneiss::render::detail::scene_resource_pin pin{{}, material};
  const auto retained = pin.capture();
  std::barrier start{2};
  bool consistent = true;
  std::thread reader([&] {
    start.arrive_and_wait();
    for (int i = 0; i < 10000; ++i) {
      auto captured = pin.capture();
      const bool is_left = captured.material == left;
      const bool is_right = captured.material == right;
      if ((!is_left && !is_right) ||
          (is_left && (captured.textures[0] != left_pixels || captured.textures[2])) ||
          (is_right && (captured.textures[2] != right_pixels || captured.textures[0])))
        consistent = false;
    }
  });
  start.arrive_and_wait();
  for (int i = 0; i < 10000; ++i) {
    if (i % 2 == 0)
      material->publish(right, right_dependencies);
    else
      material->publish(left, left_dependencies);
  }
  reader.join();
  CHECK(consistent);
  CHECK(retained.material == left && retained.textures[0] == left_pixels && !retained.textures[2]);
  return true;
}

bool target_lifetime() {
  render::service owner;
  render::service foreign;
  CHECK(!owner.create_target({0, 64}));
  CHECK(!owner.create_target({64, 16385}));
  auto target = owner.create_target({64, 32});
  auto scene = owner.create_scene();
  CHECK(target && scene);
  CHECK(!foreign.destroy_target(*target));
  CHECK(!owner.destroy_target(*scene));
  render::frame_request request;
  request.views.push_back({.scene = *scene, .target = *target});
  auto image = std::make_shared<render::frame_image>();
  // 目标提供输出尺寸；尚未初始化设备会拒绝接受，保留输入。
  auto pending = owner.request_capture(std::move(request), image);
  CHECK(!pending && pending.error() == error::not_ready && request.views.size() == 1);
  CHECK(owner.destroy_target(*target));
  CHECK(!owner.destroy_target(*target));
  auto replacement = owner.create_target({64, 32});
  CHECK(replacement && *replacement != *target);
  auto stale = owner.request_capture(std::move(request), image);
  CHECK(!stale && stale.error() == error::invalid_handle && request.views.size() == 1);
  render::device_stats stats;
  CHECK(owner.shutdown(stats).ok());
  CHECK(!owner.create_target({64, 32}));
  CHECK(!owner.destroy_target(*replacement));
  return true;
}
int main() {
  return commands() && queued_scene_consumption() && scene_extraction() && service_lifetime() &&
                 identity_versions() && concurrent_material_publication() && target_lifetime()
             ? 0
             : 1;
}
