// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/async_scene.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/scene_loading.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/scene/prefab_description.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
using namespace gneiss::engine;

void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("场景准备契约失败，行=" + std::to_string(where.line()));
  }
}

struct memory_files final : gneiss::test_support::memory_file_fixture {
  std::map<std::string, std::string> files;
  mutable std::map<std::string, unsigned> reads;
  bool change_on_verify{};
  gneiss::engine::error read(std::string_view, std::vector<std::byte>&) const noexcept override {
    return gneiss::engine::error::unsupported;
  }
  gneiss::engine::error read_bounded(std::string_view path, std::size_t limit,
                                     std::vector<std::byte>& bytes) const noexcept override try {
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::engine::error::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::engine::error::invalid_argument;
    }
    bytes.resize(found->second.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(found->second[i]);
    }
    if (++reads[std::string(path)] == 2U && change_on_verify && !bytes.empty()) {
      bytes.back() = std::byte{' '};
    }
    return gneiss::engine::error::success;
  } catch (...) {
    return gneiss::engine::error::out_of_memory;
  }
};

constexpr std::string_view object = R"({"uuid":"00000000-0000-4000-8000-000000000002",
"parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://m.mesh","material":"asset://m.material"}}]})";
constexpr std::string_view scene_prefix = R"({"format":"gneiss.scene","version":5,
"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[)";

std::string instance(std::string_view id) {
  return R"({"uuid":"00000000-0000-4000-8000-00000000000)" + std::string(id) +
         R"(","prefab":{"uri":"asset://p.prefab","overrides":[]},"parent":null,
"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}})";
}

unsigned scene_decodes{}, prefab_decodes{};
result<scene_description, serialization::diagnostic>
custom_scene(std::span<const std::byte> bytes) {
  ++scene_decodes;
  return gneiss::engine::asset_file::deserialize<scene_description>(bytes);
}
result<prefab_description, serialization::diagnostic>
custom_prefab(std::span<const std::byte> bytes) {
  ++prefab_decodes;
  return gneiss::engine::asset_file::deserialize<prefab_description>(bytes);
}
void run() {
  auto memory = std::make_shared<memory_files>();
  memory->files["main.scene"] = std::string(scene_prefix) + std::string(object) + "," +
                                instance("3") + "," + instance("4") + "]}";
  memory->files["p.prefab"] =
      R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"00000000-0000-4000-8000-000000000005","root_uuid":"00000000-0000-4000-8000-000000000002","objects":[)" +
      std::string(object) + "]}";
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", memory) == gneiss::engine::error::success);
  auto& executor = gneiss::test_support::asset_tasks();
  asset_manager assets(gneiss::test_support::test_files(files));
  check(assets.register_decoder<scene_description>(&custom_scene).ok());
  check(assets.register_decoder<prefab_description>(&custom_prefab).ok());
  assets.attach_task_executor(executor);
  scene_instantiation_data output;
  gneiss::engine::scene_diagnostic diagnostic;
  auto prepare = [&](scene_prepare_limits limits = {}, const std::function<bool()>& cancel = {}) {
    return gneiss::test_support::prepare_scene_for_test(
        assets, executor, false, "asset://main.scene", output, diagnostic, cancel, limits);
  };
  check(prepare() == gneiss::engine::error::success);
  check(output.prefabs.size() == 1U && output.assets.size() == 2U && output.instance_nodes == 3U);
  check(memory->reads["p.prefab"] == 1U &&
        output.parent_first == std::vector<std::size_t>({0U, 1U, 2U}));
  const auto bytes = memory->files.at("main.scene").size() + memory->files.at("p.prefab").size();
  check(output.source_bytes == bytes);
  // 准备结果拥有场景值副本；没有原始引用时弱缓存立即失效。
  check(assets.find_loaded<scene_description>("asset://main.scene").error() == error::not_found);
  asset_ref<const scene_description> held_scene;
  check(gneiss::test_support::load_asset(assets, "asset://main.scene", held_scene).ok());
  auto cached_request = assets.load<scene_description>("asset://main.scene");
  check(bool(cached_request));
  auto cached_scene = cached_request->take();
  check(bool(cached_scene) &&
        cached_request->source_bytes() == memory->files.at("main.scene").size());
  check(scene_decodes == 2U && prefab_decodes == 1U);
  detail::scene_description_request request(assets, "asset://main.scene", false);
  scene_preparation_inputs inputs;
  auto status = error::not_ready;
  for (unsigned i = 0; i < 100U && status == error::not_ready; ++i) {
    check(assets.async_files()->dispatch_completions(64).ok());
    assets.advance();
    status = request.advance(inputs, diagnostic);
    check(executor.run_ready().status == drive_status::success);
  }
  check(status.ok() && scene_decodes == 3U && prefab_decodes == 2U);
  const auto reads = memory->reads;
  check(prepare_loaded_scene(inputs, {}, output, diagnostic, {}).ok());
  check(memory->reads == reads &&
        output.prefabs.at("asset://p.prefab").resource == inputs.prefabs.at("asset://p.prefab"));
  // 只收集每个实例最终生效的资源；全被覆盖的来源材质不再是加载依赖。
  auto overridden = std::make_shared<scene_description>(*inputs.scene);
  std::erase_if(overridden->objects, [](const auto& node) { return !node.prefab; });
  const auto source_uuid = inputs.prefabs.at("asset://p.prefab")->root_uuid;
  for (auto& node : overridden->objects) {
    node.prefab->overrides.push_back(
        {.key = {.target = {.source_node_uuid = source_uuid,
                            .target_type_guid = mesh_renderer_description::type_guid},
                 .path = {meta::property_id{2}}},
         .value = value{std::string("asset://override.material")}});
  }
  auto overridden_inputs = inputs;
  overridden_inputs.scene = asset_ref<const scene_description>{overridden};
  check(prepare_loaded_scene(overridden_inputs, {}, output, diagnostic, {}).ok());
  check(output.assets.size() == 2);
  check(std::ranges::none_of(output.assets,
                             [](const auto& asset) { return asset.uri == "asset://m.material"; }));
  check(std::ranges::any_of(
      output.assets, [](const auto& asset) { return asset.uri == "asset://override.material"; }));
  // 空材质表示清除引用；不产生材质依赖。
  for (auto& node : overridden->objects)
    node.prefab->overrides.front().value = value{std::string{}};
  check(prepare_loaded_scene(overridden_inputs, {}, output, diagnostic, {}).ok());
  check(output.assets.size() == 1 && output.assets.front().type == render_resource_kind::mesh);
  // 非空但非法的 URI 仍在创建实体或开始资源加载前拒绝。
  overridden->objects.front().prefab->overrides.front().value =
      value{std::string{"not-an-asset-uri"}};
  check(prepare_loaded_scene(overridden_inputs, {}, output, diagnostic, {}).failed());
  check(output.assets.empty());
  check(prepare_loaded_scene(inputs, {}, output, diagnostic, {}).ok());
  asset_ref<const prefab_description> newer;
  check(gneiss::test_support::load_asset(assets, "asset://p.prefab", newer, true).ok());
  check(newer != output.prefabs.at("asset://p.prefab").resource);
  check(prepare_loaded_scene(inputs, {}, output, diagnostic, {}).ok());
  asset_ref<const prefab_description> cached;
  check(gneiss::test_support::load_asset(assets, "asset://p.prefab", cached).ok() &&
        cached == newer);

  check(gneiss::test_support::prepare_scene_for_test(assets, executor, true, "asset://p.prefab",
                                                     output, diagnostic,
                                                     {}) == gneiss::engine::error::success);
  check(output.prefabs.size() == 1U && output.instance_nodes == 1U && output.assets.size() == 2U);
  check(output.description.objects.empty());
  check(gneiss::test_support::prepare_scene_for_test(assets, executor, true, "asset://p.prefab",
                                                     output, diagnostic, [] { return true; }) ==
        gneiss::engine::error::invalid_state);
  check(output.prefabs.empty() && output.assets.empty());
  check(prepare({.source_bytes = bytes, .nodes = 3U}) == gneiss::engine::error::success);
  check(prepare({.source_bytes = bytes - 1U}) == gneiss::engine::error::invalid_argument);
  check(output.description.objects.empty() && output.prefabs.empty() && output.assets.empty());
  check(prepare({.nodes = 2U}) == gneiss::engine::error::invalid_argument);
  check(prepare({.dependencies = 1U}) == gneiss::engine::error::invalid_argument);
  check(prepare({.hierarchy_depth = 1U}) == gneiss::engine::error::success);
  check(prepare({.hierarchy_depth = 0U}) == gneiss::engine::error::invalid_argument);
  check(prepare({}, [] { return true; }) == gneiss::engine::error::invalid_state);
  // 无 Render 的统一入口：描述准备不创建运行对象，两个观察者独立取消。
  auto first_prepare = assets.prepare_scene("asset://main.scene");
  auto second_prepare = assets.prepare_scene("asset://main.scene");
  check(bool(first_prepare) && bool(second_prepare));
  first_prepare->cancel();
  for (unsigned i = 0; i < 100U && second_prepare->status() == error::not_ready; ++i) {
    check(assets.async_files()->dispatch_completions(64).ok());
    assets.advance();
    check(executor.run_ready().status == drive_status::success);
  }
  auto unified = second_prepare->take();
  check(bool(unified) && (*unified)->instance_nodes == 3U);
  auto no_renderer = assets.load<texture_resource>("asset://image");
  check(!no_renderer && no_renderer.error() == error::invalid_state);
  auto created = assets.create(scene_description{});
  check(bool(created));
  memory->reads.clear();
  memory->change_on_verify = true;
  check(prepare() == gneiss::engine::error::success && memory->reads["p.prefab"] == 1U);
  memory->change_on_verify = false;
  memory->files.erase("p.prefab");
  check(prepare() == gneiss::engine::error::not_found && output.prefabs.empty());

  // 输入顺序不是父节点优先；输出索引保留作者对象顺序并提供独立拓扑顺序。
  auto child = std::string(object);
  child.replace(child.find("000000000002"), 12U, "000000000003");
  child.replace(child.find("null"), 4U, "\"00000000-0000-4000-8000-000000000002\"");
  memory->files["main.scene"] =
      std::string(scene_prefix) + child + "," + std::string(object) + R"(]})";
  check(prepare() == gneiss::engine::error::success &&
        output.parent_first == std::vector<std::size_t>({1U, 0U}));
  check(prepare({.hierarchy_depth = 1U}) == gneiss::engine::error::invalid_argument);
  auto parent = std::string(object);
  parent.replace(parent.find("null"), 4U, "\"00000000-0000-4000-8000-000000000003\"");
  memory->files["main.scene"] = std::string(scene_prefix) + child + "," + parent + R"(]})";
  check(prepare() == gneiss::engine::error::invalid_argument && output.parent_first.empty());
  auto stopping = assets.prepare_scene("asset://main.scene");
  check(bool(stopping));
  assets.request_stop();
  check(stopping->take().error().error == error::invalid_state);
  assets.advance();
  check(assets.stopped() && (*unified)->instance_nodes == 3U);
  check(assets.prepare_scene("asset://main.scene").error() == error::invalid_state);
}
} // namespace

int main() try {
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
