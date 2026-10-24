// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"

#include "engine/resource/vfs/virtual_file_system.hpp"

#include <cstdio>
#include <map>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
using namespace engine::scene;

void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("场景准备契约失败，行=" + std::to_string(where.line()));
  }
}

struct memory_files final : engine::resource::file_system {
  std::map<std::string, std::string> files;
  mutable std::map<std::string, unsigned> reads;
  bool change_on_verify{};
  gneiss::engine::core::result read(std::string_view,
                                    std::vector<std::byte>&) const noexcept override {
    return gneiss::engine::core::result::unsupported;
  }
  gneiss::engine::core::result read_bounded(std::string_view path, std::size_t limit,
                                            std::vector<std::byte>& bytes) const noexcept override
      try {
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::engine::core::result::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::engine::core::result::invalid_argument;
    }
    bytes.resize(found->second.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(found->second[i]);
    }
    if (++reads[std::string(path)] == 2U && change_on_verify && !bytes.empty()) {
      bytes.back() = std::byte{' '};
    }
    return gneiss::engine::core::result::success;
  } catch (...) {
    return gneiss::engine::core::result::out_of_memory;
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

void run() {
  auto memory = std::make_shared<memory_files>();
  memory->files["main.scene"] = std::string(scene_prefix) + std::string(object) +
                                R"(],"prefab_instances":[)" + instance("3") + "," + instance("4") +
                                "]}";
  memory->files["p.prefab"] =
      R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"00000000-0000-4000-8000-000000000005","objects":[)" +
      std::string(object) + "]}";
  engine::resource::virtual_file_system files;
  check(files.mount("asset://", memory) == gneiss::engine::core::result::success);
  prepared_scene_description output;
  gneiss::engine::resource::scene_diagnostic diagnostic;
  auto prepare = [&](scene_prepare_limits limits = {}, const std::function<bool()>& cancel = {}) {
    return prepare_scene_description(files, "asset://main.scene", output, diagnostic, cancel,
                                     limits);
  };
  check(prepare() == gneiss::engine::core::result::success);
  check(output.prefabs.size() == 1U && output.assets.size() == 2U && output.instance_nodes == 5U);
  check(memory->reads["p.prefab"] == 1U && output.parent_first == std::vector<std::size_t>{0U});
  const auto bytes = memory->files["main.scene"].size() + memory->files["p.prefab"].size();
  check(output.source_bytes == bytes);
  check(prepare_prefab_description(files, "asset://p.prefab", output, diagnostic, {}) ==
        gneiss::engine::core::result::success);
  check(output.prefabs.size() == 1U && output.instance_nodes == 2U && output.assets.size() == 2U);
  check(output.description.objects.empty());
  check(prepare_prefab_description(files, "asset://p.prefab", output, diagnostic, [] {
          return true;
        }) == gneiss::engine::core::result::invalid_state);
  check(output.prefabs.empty() && output.assets.empty());
  check(prepare({.source_bytes = bytes, .nodes = 5U}) == gneiss::engine::core::result::success);
  check(prepare({.source_bytes = bytes - 1U}) == gneiss::engine::core::result::invalid_argument);
  check(output.description.objects.empty() && output.prefabs.empty() && output.assets.empty());
  check(prepare({.nodes = 4U}) == gneiss::engine::core::result::invalid_argument);
  check(prepare({.dependencies = 1U}) == gneiss::engine::core::result::invalid_argument);
  check(prepare({.hierarchy_depth = 1U}) == gneiss::engine::core::result::invalid_argument);
  check(prepare({.hierarchy_depth = 0U}) == gneiss::engine::core::result::invalid_argument);
  check(prepare({}, [] { return true; }) == gneiss::engine::core::result::invalid_state);
  memory->reads.clear();
  memory->change_on_verify = true;
  check(prepare() == gneiss::engine::core::result::success && memory->reads["p.prefab"] == 1U);
  memory->change_on_verify = false;
  memory->files.erase("p.prefab");
  check(prepare() == gneiss::engine::core::result::not_found && output.prefabs.empty());

  // 输入顺序不是父节点优先；输出索引保留作者对象顺序并提供独立拓扑顺序。
  auto child = std::string(object);
  child.replace(child.find("000000000002"), 12U, "000000000003");
  child.replace(child.find("null"), 4U, "\"00000000-0000-4000-8000-000000000002\"");
  memory->files["main.scene"] =
      std::string(scene_prefix) + child + "," + std::string(object) + R"(],"prefab_instances":[]})";
  check(prepare() == gneiss::engine::core::result::success &&
        output.parent_first == std::vector<std::size_t>({1U, 0U}));
  check(prepare({.hierarchy_depth = 1U}) == gneiss::engine::core::result::invalid_argument);
  auto parent = std::string(object);
  parent.replace(parent.find("null"), 4U, "\"00000000-0000-4000-8000-000000000003\"");
  memory->files["main.scene"] =
      std::string(scene_prefix) + child + "," + parent + R"(],"prefab_instances":[]})";
  check(prepare() == gneiss::engine::core::result::invalid_argument && output.parent_first.empty());
}
} // namespace

int main() try {
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
