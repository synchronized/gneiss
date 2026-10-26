// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/async_scene.hpp"
#include "../support/memory_read_file_system.hpp"
#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_manager.hpp"
#include "engine/resource/loaders/scene_serialization.hpp"

#include "engine/resource/vfs/virtual_file_system.hpp"

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

struct memory_files final : gneiss::test_support::memory_read_file_system {
  std::map<std::string, std::string> files;
  mutable std::map<std::string, unsigned> reads;
  bool change_on_verify{};
  gneiss::engine::result read(std::string_view, std::vector<std::byte>&) const noexcept override {
    return gneiss::engine::result::unsupported;
  }
  gneiss::engine::result read_bounded(std::string_view path, std::size_t limit,
                                      std::vector<std::byte>& bytes) const noexcept override try {
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::engine::result::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::engine::result::invalid_argument;
    }
    bytes.resize(found->second.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(found->second[i]);
    }
    if (++reads[std::string(path)] == 2U && change_on_verify && !bytes.empty()) {
      bytes.back() = std::byte{' '};
    }
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::out_of_memory;
  }
};

constexpr std::string_view object = R"({"uuid":"00000000-0000-4000-8000-000000000002",
"parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
"components":{"mesh_renderer":{"mesh":"asset://m.mesh","material":"asset://m.material"}}})";
constexpr std::string_view scene_prefix = R"({"format":"gneiss.scene","version":4,
"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[)";

std::string instance(std::string_view id) {
  return R"({"instance_uuid":"00000000-0000-4000-8000-00000000000)" + std::string(id) +
         R"(","prefab":"asset://p.prefab","parent":null,
"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"overrides":[]})";
}

unsigned scene_decodes{}, prefab_decodes{};
result custom_scene(std::span<const std::byte> bytes, scene_description& value,
                    gneiss::engine::meta::serialization_diagnostic& diagnostic) {
  ++scene_decodes;
  return decode_scene(bytes, value, diagnostic);
}
result custom_prefab(std::span<const std::byte> bytes, prefab_description& value,
                     gneiss::engine::meta::serialization_diagnostic& diagnostic) {
  ++prefab_decodes;
  return decode_prefab(bytes, value, diagnostic);
}
void run() {
  auto memory = std::make_shared<memory_files>();
  memory->files["main.scene"] = std::string(scene_prefix) + std::string(object) +
                                R"(],"prefab_instances":[)" + instance("3") + "," + instance("4") +
                                "]}";
  memory->files["p.prefab"] =
      R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"00000000-0000-4000-8000-000000000005","objects":[)" +
      std::string(object) + "]}";
  gneiss::engine::virtual_file_system files;
  check(files.mount("asset://", memory) == gneiss::engine::result::success);
  auto& executor = gneiss::test_support::asset_tasks();
  asset_manager assets(gneiss::test_support::test_files(files));
  check(assets
            .register_decoder<scene_description>(&custom_scene,
                                                 scene_description::maximum_source_bytes)
            .ok());
  check(assets
            .register_decoder<prefab_description>(&custom_prefab,
                                                  prefab_description::maximum_source_bytes)
            .ok());
  assets.attach_task_executor(executor);
  prepared_scene_description output;
  gneiss::engine::scene_diagnostic diagnostic;
  auto prepare = [&](scene_prepare_limits limits = {}, const std::function<bool()>& cancel = {}) {
    return gneiss::test_support::prepare_scene_for_test(
        assets, executor, false, "asset://main.scene", output, diagnostic, cancel, limits);
  };
  check(prepare() == gneiss::engine::result::success);
  check(output.prefabs.size() == 1U && output.assets.size() == 2U && output.instance_nodes == 5U);
  check(memory->reads["p.prefab"] == 1U && output.parent_first == std::vector<std::size_t>{0U});
  const auto bytes = output.description.author_json.size() +
                     output.prefabs.begin()->second.resource->author_json.size();
  check(output.source_bytes == bytes);
  check(scene_decodes == 1U && prefab_decodes == 1U);
  scene_description_request request(assets, "asset://main.scene", false);
  scene_description_inputs inputs;
  auto status = result::not_ready;
  for (unsigned i = 0; i < 100U && status == result::not_ready; ++i) {
    check(assets.async_files()->dispatch_completions(64).ok());
    assets.advance_resources();
    status = request.advance(inputs, diagnostic);
    check(executor.run_ready().status == drive_status::success);
  }
  check(status.ok() && scene_decodes == 2U && prefab_decodes == 2U);
  const auto reads = memory->reads;
  check(prepare_loaded_scene(inputs, {}, output, diagnostic, {}).ok());
  check(memory->reads == reads &&
        output.prefabs.at("asset://p.prefab").resource == inputs.prefabs.at("asset://p.prefab"));
  asset_ref<const prefab_description> newer;
  check(gneiss::test_support::load_asset(assets, "asset://p.prefab", newer, true).ok());
  check(newer != output.prefabs.at("asset://p.prefab").resource);
  check(prepare_loaded_scene(inputs, {}, output, diagnostic, {}).ok());
  asset_ref<const prefab_description> cached;
  check(gneiss::test_support::load_asset(assets, "asset://p.prefab", cached).ok() &&
        cached == newer);

  check(gneiss::test_support::prepare_scene_for_test(assets, executor, true, "asset://p.prefab",
                                                     output, diagnostic,
                                                     {}) == gneiss::engine::result::success);
  check(output.prefabs.size() == 1U && output.instance_nodes == 2U && output.assets.size() == 2U);
  check(output.description.objects.empty());
  check(gneiss::test_support::prepare_scene_for_test(assets, executor, true, "asset://p.prefab",
                                                     output, diagnostic, [] { return true; }) ==
        gneiss::engine::result::invalid_state);
  check(output.prefabs.empty() && output.assets.empty());
  check(prepare({.source_bytes = bytes, .nodes = 5U}) == gneiss::engine::result::success);
  check(prepare({.source_bytes = bytes - 1U}) == gneiss::engine::result::invalid_argument);
  check(output.description.objects.empty() && output.prefabs.empty() && output.assets.empty());
  check(prepare({.nodes = 4U}) == gneiss::engine::result::invalid_argument);
  check(prepare({.dependencies = 1U}) == gneiss::engine::result::invalid_argument);
  check(prepare({.hierarchy_depth = 1U}) == gneiss::engine::result::invalid_argument);
  check(prepare({.hierarchy_depth = 0U}) == gneiss::engine::result::invalid_argument);
  check(prepare({}, [] { return true; }) == gneiss::engine::result::invalid_state);
  memory->reads.clear();
  memory->change_on_verify = true;
  check(prepare() == gneiss::engine::result::success && memory->reads["p.prefab"] == 1U);
  memory->change_on_verify = false;
  memory->files.erase("p.prefab");
  check(prepare() == gneiss::engine::result::not_found && output.prefabs.empty());

  // 输入顺序不是父节点优先；输出索引保留作者对象顺序并提供独立拓扑顺序。
  auto child = std::string(object);
  child.replace(child.find("000000000002"), 12U, "000000000003");
  child.replace(child.find("null"), 4U, "\"00000000-0000-4000-8000-000000000002\"");
  memory->files["main.scene"] =
      std::string(scene_prefix) + child + "," + std::string(object) + R"(],"prefab_instances":[]})";
  check(prepare() == gneiss::engine::result::success &&
        output.parent_first == std::vector<std::size_t>({1U, 0U}));
  check(prepare({.hierarchy_depth = 1U}) == gneiss::engine::result::invalid_argument);
  auto parent = std::string(object);
  parent.replace(parent.find("null"), 4U, "\"00000000-0000-4000-8000-000000000003\"");
  memory->files["main.scene"] =
      std::string(scene_prefix) + child + "," + parent + R"(],"prefab_instances":[]})";
  check(prepare() == gneiss::engine::result::invalid_argument && output.parent_first.empty());
}
} // namespace

int main() try {
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
