// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/async_scene.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/scene/prefab_codec.hpp"
#include "engine/resource/scene/scene_codec.hpp"

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
result<scene_description, meta::serialization_diagnostic>
custom_scene(std::span<const std::byte> bytes) {
  ++scene_decodes;
  return decode_scene(bytes);
}
result<prefab_description, meta::serialization_diagnostic>
custom_prefab(std::span<const std::byte> bytes) {
  ++prefab_decodes;
  return decode_prefab(bytes);
}
void run() {
  auto memory = std::make_shared<memory_files>();
  memory->files["main.scene"] = std::string(scene_prefix) + std::string(object) + "," +
                                instance("3") + "," + instance("4") + "]}";
  memory->files["p.prefab"] =
      R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"00000000-0000-4000-8000-000000000005","objects":[)" +
      std::string(object) + "]}";
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", memory) == gneiss::engine::error::success);
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
  check(prepare() == gneiss::engine::error::success);
  check(output.prefabs.size() == 1U && output.assets.size() == 2U && output.instance_nodes == 3U);
  check(memory->reads["p.prefab"] == 1U &&
        output.parent_first == std::vector<std::size_t>({0U, 1U, 2U}));
  const auto bytes = output.description.author_json.size() +
                     output.prefabs.begin()->second.resource->author_json.size();
  check(output.source_bytes == bytes);
  check(scene_decodes == 1U && prefab_decodes == 1U);
  scene_description_request request(assets, "asset://main.scene", false);
  scene_description_inputs inputs;
  auto status = error::not_ready;
  for (unsigned i = 0; i < 100U && status == error::not_ready; ++i) {
    check(assets.async_files()->dispatch_completions(64).ok());
    assets.advance();
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
