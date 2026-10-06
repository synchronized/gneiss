// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>

#include <cstdlib>
#include <exception>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

int main(int argc, [[maybe_unused]] char** argv) {
  using gneiss::result;
  using gneiss::scene_prefab_refresh;
  static_assert(!std::is_copy_constructible_v<scene_prefab_refresh>);
  static_assert(std::is_nothrow_move_constructible_v<scene_prefab_refresh>);
  static_assert(std::is_nothrow_move_assignable_v<scene_prefab_refresh>);
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view uri = "asset://scenes/prefab.scene.json";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss::application app;
  gneiss::scene_instance scene;
  if (gneiss::application::create_native(desc, app).failed() ||
      gneiss::scene_instance::load(app.get(), uri, scene).failed()) {
    return 1;
  }
  gneiss::scene_prefab_node_info info = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  if (scene.get_prefab_node_info(0U, info).failed()) {
    return 2;
  }
  gneiss::scene_node_id root{info.node};
  const auto initial_root = root;
  scene_prefab_refresh first;
  if (first.reset().failed() ||
      scene.refresh_prefab_instance({}, root, first) != result::invalid_handle ||
      root != initial_root || first || scene.refresh_prefab_instance(root, root, first).failed() ||
      root == initial_root || !first || first.owner() != app.get() ||
      first.scene() != scene.get()) {
    return 3;
  }
  if (argc > 1) {
    std::thread worker([resource = std::move(first)]() mutable {
      std::set_terminate([] { std::_Exit(73); });
      scene_prefab_refresh local{std::move(resource)};
    });
    worker.join();
    return 4;
  }
  const auto original_token = first.get();
  const auto refreshed_root = root;
  // 非空输出必须在改变场景前被拒绝，不能隐式丢失先前历史。
  if (scene.refresh_prefab_instance(root, root, first) != result::invalid_state ||
      root != refreshed_root || first.get() != original_token ||
      scene.get_prefab_node_info(0U, info).failed() || info.node != root.get()) {
    return 5;
  }
  result reset_status;
  result toggle_status;
  result refresh_status;
  scene_prefab_refresh wrong_thread_output;
  std::thread worker([&] {
    reset_status = first.reset();
    toggle_status = first.toggle(root);
    refresh_status = scene.refresh_prefab_instance(root, root, wrong_thread_output);
  });
  worker.join();
  if (reset_status != result::invalid_state || toggle_status != result::invalid_state ||
      refresh_status != result::invalid_state || wrong_thread_output || root != refreshed_root ||
      first.get() != original_token || first.toggle(root).failed() || root == refreshed_root ||
      first.toggle(root).failed()) {
    return 6;
  }
  scene_prefab_refresh second{std::move(first)};
  // NOLINTNEXTLINE(bugprone-use-after-move): 移动源必须清空所有父句柄。
  if (first || first.owner() != 0U || first.scene() != 0U || second.get() != original_token) {
    return 7;
  }
  scene_prefab_refresh third;
  if (scene.refresh_prefab_instance(root, root, third).failed()) {
    return 8;
  }
  const auto replaced = third.get();
  third = std::move(second);
  // NOLINTNEXTLINE(bugprone-use-after-move): 移动覆盖释放旧令牌并清空源。
  if (second || third.get() != original_token ||
      scene.release_prefab_refresh(replaced) != result::invalid_handle) {
    return 9;
  }
  const auto owner = third.owner();
  const auto parent_scene = third.scene();
  const auto released = third.release();
  if (third || third.owner() != 0U || third.scene() != 0U || released != original_token ||
      gneiss_scene_instance_release_prefab_refresh(owner, parent_scene, released) !=
          GNEISS_SUCCESS ||
      third.reset().failed() || third.toggle(root) != result::invalid_argument) {
    return 10;
  }
  gneiss_scene_prefab_refresh_token scoped_token{};
  {
    scene_prefab_refresh scoped;
    if (scene.refresh_prefab_instance(root, root, scoped).failed()) {
      return 11;
    }
    scoped_token = scoped.get();
  }
  if (scene.release_prefab_refresh(scoped_token) != result::invalid_handle ||
      scene.get_prefab_node_info(0U, info).failed() || info.node != root.get() ||
      scene.refresh_prefab_instance(root, root, third).failed() ||
      scene.release_prefab_refresh(third.get()).failed() || third.reset().failed() || third ||
      scene.refresh_prefab_instance(root, root, third).failed()) {
    return 12;
  }
  // 场景移动不改变令牌所属句柄；场景卸载使令牌失效，但 reset 可安全完成。
  gneiss::scene_instance moved_scene{std::move(scene)};
  if (third.toggle(root).failed() || moved_scene.reset().failed() ||
      third.toggle(root) != result::invalid_handle || third.reset().failed() || third ||
      third.reset().failed()) {
    return 13;
  }
  if (gneiss::scene_instance::load(app.get(), uri, scene).failed() ||
      scene.get_prefab_node_info(0U, info).failed() ||
      scene.refresh_prefab_instance(gneiss::scene_node_id{info.node}, root, third).failed() ||
      app.reset().failed() || third.toggle(root) != result::invalid_handle ||
      third.reset().failed() || third) {
    return 14;
  }
  return 0;
}
