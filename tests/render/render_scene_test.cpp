// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/render/detail/render_scene_state.hpp"
#include "engine/function/render/render_service.hpp"
#include <barrier>
#include <iostream>
#include <latch>
#include <limits>
#include <thread>
using namespace gneiss;
#define CHECK(expression)                                                                          \
  do {                                                                                             \
    if (!(expression)) {                                                                           \
      std::cerr << __LINE__ << ": " #expression "\n";                                              \
      return false;                                                                                \
    }                                                                                              \
  } while (false)
bool commands() {
  auto mesh = std::make_shared<detail::resource_identity<mesh_data>>();
  auto material = std::make_shared<detail::resource_identity<material_data>>();
  bool ready = true;
  auto pin = [&](const mesh_instance_description&) -> result<detail::scene_resource_pin> {
    if (!ready)
      return error::not_ready;
    return detail::scene_resource_pin{mesh, material};
  };
  detail::render_scene_state state{65000, pin};
  detail::render_scene_state other{65001, pin};
  auto scene = state.create_scene();
  auto second = state.create_scene();
  CHECK(scene && second);
  const render::instance_key id{1};
  math::matrix4 model{1.F};
  model[1][0] = .5F;
  model[3][0] = 3.F;
  render::scene_commands batch{
      *scene,
      {render::create_instance{id, {.mesh = 1, .material = 2}}, render::set_transform{id, model}}};
  CHECK(!state.find(*scene, id));
  CHECK(!other.submit(std::move(batch)));
  CHECK(batch.entries.size() == 2);
  CHECK(state.submit(std::move(batch)));
  CHECK(batch.entries.empty() && batch.scene == null_rid);
  CHECK(!state.submit(std::move(batch)));
  CHECK(state.find(*scene, id)->description.world_matrix == model);
  const auto expected_normal = math::matrix4{glm::transpose(glm::inverse(math::matrix3{model}))};
  CHECK(state.find(*scene, id)->normal_matrix == expected_normal);
  // 矩阵元素有限且行列式非零，逆矩阵仍可能溢出；应在接受时拒绝。
  math::matrix4 overflow{1.F};
  overflow[0][0] = std::numeric_limits<float>::denorm_min();
  render::scene_commands invalid_normal{*scene, {render::set_transform{id, overflow}}};
  auto rejected_normal = state.submit(std::move(invalid_normal));
  CHECK(!rejected_normal && rejected_normal.error() == error::invalid_argument);
  CHECK(invalid_normal.entries.size() == 1 &&
        state.find(*scene, id)->description.world_matrix == model);
  CHECK(state.find(*scene, id)->normal_matrix == expected_normal);
  render::scene_commands invalid_create{
      *scene, {render::create_instance{{2}, {.mesh = 1, .material = 2, .world_matrix = overflow}}}};
  CHECK(!state.submit(std::move(invalid_create)) && invalid_create.entries.size() == 1);
  CHECK(!state.find(*scene, {2}));
  render::scene_commands wrong_scene{*second, {render::destroy_instance{id}}};
  CHECK(!state.submit(std::move(wrong_scene)));
  {
    render::scene_commands abandoned{*scene,
                                     {render::create_instance{{2}, {.mesh = 1, .material = 2}},
                                      render::set_visibility{id, {.visible = false}}}};
  }
  CHECK(!state.find(*scene, {2}) && state.find(*scene, id)->description.visible);
  // 后一个无效命令使整个列表拒绝；已有状态、创建身份和调用方列表保持原样。
  render::scene_commands invalid{*scene,
                                 {render::set_visibility{id, {.visible = false}},
                                  render::create_instance{{2}, {.mesh = 1, .material = 2}},
                                  render::set_transform{id, math::matrix4{0.F}}}};
  CHECK(!state.submit(std::move(invalid)));
  CHECK(invalid.entries.size() == 3 && state.find(*scene, id)->description.visible);
  CHECK(!state.find(*scene, {2}));
  invalid.entries.pop_back();
  ready = false;
  auto pending = state.submit(std::move(invalid));
  CHECK(!pending && pending.error() == error::not_ready);
  CHECK(state.find(*scene, id)->description.visible);
  ready = true;
  CHECK(state.submit(std::move(invalid)));
  CHECK(!state.find(*scene, id)->description.visible && state.find(*scene, {2}));
  // 换绑失败不释放旧依赖；成功后新依赖存活，字段更新不覆盖矩阵。
  auto old_material = std::weak_ptr<detail::resource_identity<material_data>>{material};
  material = std::make_shared<detail::resource_identity<material_data>>();
  render::scene_commands rebind{*scene, {render::set_resources{id, 4, 5}}};
  ready = false;
  CHECK(!state.submit(std::move(rebind)));
  CHECK(state.find(*scene, id)->description.mesh == 1);
  ready = true;
  CHECK(state.submit(std::move(rebind)));
  CHECK(state.find(*scene, id)->resources.material == material);
  CHECK(state.find(*scene, id)->description.world_matrix == model);
  CHECK(!old_material.expired()); // 第二个实例仍持有旧材质。
  // 删除、同列表创建再删除与旧 key 重用。
  render::scene_commands deletion{*scene,
                                  {render::destroy_instance{{2}},
                                   render::create_instance{{3}, {.mesh = 1, .material = 2}},
                                   render::destroy_instance{{3}}}};
  CHECK(state.submit(std::move(deletion)));
  CHECK(!state.find(*scene, {2}) && !state.find(*scene, {3}));
  render::scene_commands stale{*scene, {render::destroy_instance{{2}}}};
  CHECK(!state.submit(std::move(stale)));
  render::scene_commands reuse{*scene, {render::create_instance{{3}, {.mesh = 1, .material = 2}}}};
  CHECK(!state.submit(std::move(reuse)));
  render::scene_commands zero{*scene, {render::create_instance{{}, {.mesh = 1, .material = 2}}}};
  CHECK(!state.submit(std::move(zero)));
  render::scene_commands duplicate{*scene,
                                   {render::destroy_instance{id}, render::destroy_instance{id}}};
  CHECK(!state.submit(std::move(duplicate)) && state.find(*scene, id));
  // 不同场景可以复用局部 key；最大 key 不允许回绕。
  render::scene_commands another{
      *second,
      {render::create_instance{id, {.mesh = 1, .material = 2}},
       render::create_instance{{std::numeric_limits<std::uint64_t>::max()},
                               {.mesh = 1, .material = 2}}}};
  CHECK(state.submit(std::move(another)));
  CHECK(state.find(*second, id));
  render::scene_commands wrapped{*second,
                                 {render::create_instance{{2}, {.mesh = 1, .material = 2}}}};
  CHECK(!state.submit(std::move(wrapped)));
  auto held = std::weak_ptr<detail::resource_identity<mesh_data>>{mesh};
  mesh.reset();
  CHECK(!held.expired());
  CHECK(state.destroy_scene(*scene));
  CHECK(!state.find(*scene, id) && state.find(*second, id));
  CHECK(!held.expired());
  CHECK(!state.destroy_scene(*scene));
  auto replacement = state.create_scene();
  CHECK(replacement && *replacement != *scene);
  CHECK(!state.submit(std::move(stale)));
  state.close();
  CHECK(held.expired());
  CHECK(!state.create_scene());
  CHECK(!state.submit(std::move(wrapped)));
  return true;
}
bool queued_scene_consumption() {
  auto mesh = std::make_shared<detail::resource_identity<mesh_data>>();
  auto material = std::make_shared<detail::resource_identity<material_data>>();
  std::weak_ptr retained_mesh = mesh;
  threaded_render_executor executor;
  CHECK(executor.initialize([](auto&, auto&) { return error::success; }).ok());
  std::latch entered{1};
  std::latch release{1};
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
  auto enqueue = [&](std::function<void()> operation) -> result<void> {
    auto accepted = executor.submit_command([operation = std::move(operation)](const auto&) {
      operation();
      return error::success;
    });
    return accepted ? result<void>{} : result<void>{accepted.error()};
  };
  bool accepting = true;
  const auto producer = std::this_thread::get_id();
  detail::render_scene_state state{
      64997,
      [&](const mesh_instance_description&) -> result<detail::scene_resource_pin> {
        if (std::this_thread::get_id() != producer)
          return error::invalid_state;
        return detail::scene_resource_pin{mesh, material};
      },
      [&](std::function<void()> operation) -> result<void> {
        if (!accepting)
          return error::not_ready;
        return enqueue(std::move(operation));
      }};
  auto scene = state.create_scene();
  CHECK(scene);
  render::scene_commands create{*scene, {render::create_instance{{1}, {.mesh = 1, .material = 2}}}};
  CHECK(state.submit(std::move(create)));
  CHECK(!state.find(*scene, {1})); // 已接受但尚未执行。
  render::frame_request request{.window = {.width = 64, .height = 64},
                                .views = {{.scene = *scene}}};
  std::vector<result<render_frame_packet>> frames;
  auto first = state.prepare_capture(request);
  CHECK(first);
  CHECK(enqueue([operation = std::move(*first), &frames] { frames.push_back(operation()); }));
  math::matrix4 moved{1.F};
  moved[3][0] = 5.F;
  render::scene_commands update{*scene, {render::set_transform{{1}, moved}}};
  accepting = false;
  CHECK(state.submit(std::move(update)).error() == error::not_ready);
  CHECK(update.entries.size() == 1);
  accepting = true;
  CHECK(state.submit(std::move(update))); // 按已接受身份验证，不要求创建已执行。
  auto second = state.prepare_capture(request);
  CHECK(second);
  CHECK(enqueue([operation = std::move(*second), &frames] { frames.push_back(operation()); }));
  accepting = false;
  CHECK(state.destroy_scene(*scene).error() == error::not_ready);
  CHECK(state.prepare_capture(request));
  accepting = true;
  CHECK(state.destroy_scene(*scene));
  CHECK(!state.prepare_capture(request));
  auto replacement = state.create_scene();
  CHECK(replacement && *replacement != *scene);
  render::scene_commands recreate{*replacement,
                                  {render::create_instance{{1}, {.mesh = 1, .material = 2}}}};
  CHECK(state.submit(std::move(recreate)));
  request.views.front().scene = *replacement;
  auto third = state.prepare_capture(request);
  CHECK(third);
  CHECK(enqueue([operation = std::move(*third), &frames] { frames.push_back(operation()); }));
  mesh.reset();
  material.reset();
  CHECK(!retained_mesh.expired());
  // 同一有序消费者独占实际场景数据；任务不读取接受侧 RID 表或调用方命令。
  released = true;
  release.count_down();
  CHECK(executor.flush().ok());
  executor.stop();
  CHECK(frames.size() == 3 && frames[0] && frames[1] && frames[2]);
  CHECK(frames[0]->scene.instances.size() == 1 && frames[1]->scene.instances.size() == 1 &&
        frames[2]->scene.instances.size() == 1);
  CHECK(frames[0]->scene.instances[0].world_matrix[3][0] == 0.F);
  CHECK(frames[1]->scene.instances[0].world_matrix[3][0] == 5.F);
  CHECK(frames[2]->scene.instances[0].world_matrix[3][0] == 0.F);
  state.close();
  CHECK(retained_mesh.expired());
  return true;
}

bool service_lifetime() {
  render::scene_commands pending;
  {
    render_service owner;
    render_service foreign;
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
    owner.close_resources();
    CHECK(retained.expired());
    CHECK(!owner.submit(std::move(pending)));
  }
  CHECK(pending.entries.size() == 1); // 命令可晚于服务销毁，不访问服务内存。
  return true;
}
bool identity_versions() {
  auto mesh = std::make_shared<detail::resource_identity<mesh_data>>();
  auto material = std::make_shared<detail::resource_identity<material_data>>();
  auto texture = std::make_shared<detail::resource_identity<texture_data>>();
  detail::render_scene_state state{
      64999, [&](const mesh_instance_description&) -> result<detail::scene_resource_pin> {
        return detail::scene_resource_pin{mesh, material};
      }};
  auto scene = state.create_scene();
  CHECK(scene);
  render::scene_commands create{*scene, {render::create_instance{{1}, {.mesh = 1, .material = 2}}}};
  CHECK(state.submit(std::move(create)));
  auto capture = [&] { return state.find(*scene, {1})->resources.capture(); };
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
  CHECK(state.destroy_scene(*scene));
  CHECK(after.mesh && after.material && after.textures[0]); // 已捕获内容跨场景卸载存活。
  return true;
}
bool concurrent_material_publication() {
  auto material = std::make_shared<detail::resource_identity<material_data>>();
  auto left = std::make_shared<material_data>();
  auto right = std::make_shared<material_data>();
  left->base_color_texture = 3;
  right->normal_texture = 4;
  auto left_texture = std::make_shared<detail::resource_identity<texture_data>>();
  auto right_texture = std::make_shared<detail::resource_identity<texture_data>>();
  auto left_pixels = std::make_shared<texture_data>();
  auto right_pixels = std::make_shared<texture_data>();
  left_texture->publish(left_pixels);
  right_texture->publish(right_pixels);
  detail::resource_identity<material_data>::dependencies left_dependencies{left_texture};
  detail::resource_identity<material_data>::dependencies right_dependencies{};
  right_dependencies[2] = right_texture;
  material->publish(left, left_dependencies);
  detail::scene_resource_pin pin{{}, material};
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

bool frame_capture() {
  auto mesh = std::make_shared<detail::resource_identity<mesh_data>>();
  auto material = std::make_shared<detail::resource_identity<material_data>>();
  auto texture = std::make_shared<detail::resource_identity<texture_data>>();
  detail::render_scene_state state{
      64998, [&](const mesh_instance_description&) -> result<detail::scene_resource_pin> {
        return detail::scene_resource_pin{mesh, material};
      }};
  auto scene = state.create_scene();
  CHECK(scene);
  math::matrix4 model{1.F};
  model[1][0] = 0.5F;
  model[3][0] = 4.F;
  render::scene_commands create{
      *scene,
      {render::create_instance{{1}, {.mesh = 1, .material = 2, .world_matrix = model, .layers = 2}},
       render::create_instance{{2}, {.mesh = 1, .material = 2, .visible = false}},
       render::create_instance{{3}, {.mesh = 1, .material = 2, .layers = 4}}}};
  CHECK(state.submit(std::move(create)));
  render::frame_request request{.window = {.width = 64, .height = 64},
                                .views = {{.scene = *scene, .layers = 2}}};
  auto pending = state.capture(request);
  CHECK(pending && pending->scene.instances.size() == 1);
  CHECK(!pending->resources.share_mesh(1));
  mesh->publish(std::make_shared<mesh_data>());
  auto description = std::make_shared<material_data>();
  description->base_color_texture = 3;
  material->publish(description, {texture});
  texture->publish(std::make_shared<texture_data>());
  auto before = state.capture(request);
  CHECK(before && before->scene.instances.size() == 1);
  CHECK(before->scene.instances[0].world_matrix == model);
  const auto expected_normal = math::matrix4{glm::transpose(glm::inverse(math::matrix3{model}))};
  CHECK(before->scene.instances[0].normal_matrix == expected_normal);
  CHECK(before->scene.instances[0].layers == 2 && before->scene.layers == 2);
  CHECK(before->resources.share_mesh(1) == mesh->read());
  CHECK(before->resources.share_texture(3) == texture->read());
  mesh->publish(std::make_shared<mesh_data>());
  texture->publish(std::make_shared<texture_data>());
  render::scene_commands update{*scene, {render::set_transform{{1}, math::matrix4{1.F}}}};
  CHECK(state.submit(std::move(update)));
  auto after = state.capture(request);
  CHECK(after && after->resources.share_mesh(1) != before->resources.share_mesh(1));
  CHECK(after->resources.share_texture(3) != before->resources.share_texture(3));
  CHECK(before->scene.instances[0].world_matrix == model);
  request.views.push_back(request.views.front());
  CHECK(!state.capture(request));
  request.views.pop_back();
  render::scene_commands no_shadow{
      *scene, {render::set_visibility{{1}, {.layers = 2, .casts_shadows = false}}}};
  CHECK(state.submit(std::move(no_shadow)));
  auto unsupported = state.capture(request);
  CHECK(!unsupported && unsupported.error() == error::unsupported);
  CHECK(state.destroy_scene(*scene));
  CHECK(!state.capture(request));
  CHECK(before->resources.share_mesh(1) && before->resources.share_texture(3));
  // 没有活动相机时允许零视图，窗口仍可提交 UI。
  request.views.clear();
  auto no_camera = state.capture(request);
  CHECK(no_camera && !no_camera->scene.has_camera && no_camera->scene.instances.empty());
  request.views.push_back({});
  render_service renderer;
  auto not_initialized = renderer.create_scene();
  CHECK(not_initialized);
  request.views.front().scene = *not_initialized;
  CHECK(!renderer.render(std::move(request)) && request.views.size() == 1);
  auto no_image = renderer.request_capture(std::move(request), {});
  CHECK(!no_image && no_image.error() == error::invalid_argument && request.views.size() == 1);
  auto image = std::make_shared<frame_image>();
  image->width = 17;
  auto rejected = renderer.request_capture(std::move(request), image);
  CHECK(!rejected && request.views.size() == 1 && image->width == 17);
  return true;
}
int main() {
  return commands() && queued_scene_consumption() && service_lifetime() && identity_versions() &&
                 concurrent_material_publication() && frame_capture()
             ? 0
             : 1;
}
