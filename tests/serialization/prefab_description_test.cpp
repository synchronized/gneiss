// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_tree.hpp"
#include "engine/resource/asset_manager.hpp"
#include "engine/resource/cache/resource_cache.hpp"
#include "engine/resource/resource_registry.hpp"
#include "engine/resource/types/prefab_description.hpp"
#include "engine/resource/vfs/file_system.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view valid_prefab = R"({
  "format":"gneiss.prefab",
  "version":1,
  "prefab_uuid":"10000000-0000-4000-8000-000000000001",
  "objects":[
    {
      "uuid":"10000000-0000-4000-8000-000000000002",
      "name":"Prefab Root",
      "parent":null,
      "transform":{"translation":[1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":{"mesh_renderer":{"mesh":"asset://models/lantern.mesh","material":"asset://materials/lantern.material"}}
    },
    {
      "uuid":"10000000-0000-4000-8000-000000000003",
      "name":"Child",
      "parent":"10000000-0000-4000-8000-000000000002",
      "transform":{"translation":[0,1,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":{}
    }
  ]
})";

[[nodiscard]] std::string replace_once(std::string text, std::string_view source,
                                       std::string_view replacement) {
  const auto position = text.find(source);
  if (position != std::string::npos) {
    text.replace(position, source.size(), replacement);
  }
  return text;
}

class memory_file_system final : public gneiss::engine::file_system {
public:
  explicit memory_file_system(std::string content) : content_(std::move(content)) {}

  [[nodiscard]] gneiss::engine::result
  read(std::string_view path, std::vector<std::byte>& out_bytes) const noexcept override {
    if (path != "lamp.prefab.json") {
      return gneiss::engine::result::not_found;
    }
    try {
      ++read_count;
      out_bytes.resize(content_.size());
      for (std::size_t index = 0; index < content_.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content_[index]);
      }
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }

  mutable std::size_t read_count = 0;

  gneiss::engine::result read_bounded(std::string_view path, std::size_t limit,
                                      std::vector<std::byte>& output) const noexcept override {
    if (content_.size() > limit)
      return gneiss::engine::result::invalid_argument;
    return read(path, output);
  }

private:
  std::string content_;
};

} // namespace

bool prepared_description_contract() {
  using namespace gneiss::engine;
  using namespace gneiss::engine;

  virtual_file_system files;
  auto backend = std::make_shared<memory_file_system>(std::string(valid_prefab));
  if (files.mount("asset://prefabs/", backend) != gneiss::engine::result::success) {
    return false;
  }

  virtual_file_system different_source;
  if (different_source.mount("asset://prefabs/", backend) != gneiss::engine::result::success) {
    return false;
  }
  asset_manager assets(files);
  auto& cache = assets.cache();
  asset_manager other_assets(different_source);
  constexpr std::string_view uri = "asset://prefabs/lamp.prefab.json";
  gneiss::engine::prefab_description description;
  gneiss::engine::scene_diagnostic diagnostic;
  if (gneiss::engine::parse_prefab_description(valid_prefab, description, diagnostic) !=
      gneiss::engine::result::success) {
    return false;
  }
  gneiss::engine::prefab_asset_ref legacy;
  if (assets.load_sync(uri, legacy) != gneiss::engine::result::success) {
    return false;
  }
  gneiss::engine::prefab_asset_ref first, reused, changed, foreign;
  if (assets.store<gneiss::engine::prefab_description>(
          uri, std::make_shared<const gneiss::engine::prefab_description>(description), first) !=
          gneiss::engine::result::success ||
      assets.store<gneiss::engine::prefab_description>(
          uri, std::make_shared<const gneiss::engine::prefab_description>(description), reused) !=
          gneiss::engine::result::success ||
      first.get() == reused.get() || first.get() == legacy.get() || backend->read_count != 1U) {
    return false;
  }
  // 替换当前描述，不改写已实例化场景持有的旧描述。
  description.objects[0].translation[0] = 37.0F;
  if (assets.store<gneiss::engine::prefab_description>(
          uri, std::make_shared<const gneiss::engine::prefab_description>(description), changed) !=
          gneiss::engine::result::success ||
      changed.get() == first.get() || changed.get()->objects[0].translation[0] != 37.0F ||
      first.get()->objects[0].translation[0] != 1.0F ||
      other_assets.store<gneiss::engine::prefab_description>(
          uri, std::make_shared<const gneiss::engine::prefab_description>(description), foreign) !=
          gneiss::engine::result::success ||
      foreign.get() == changed.get() || backend->read_count != 1U) {
    return false;
  }
  gneiss::engine::prefab_asset_ref ordinary;
  if (assets.load_sync(uri, ordinary) != gneiss::engine::result::success ||
      ordinary.get() != changed.get()) {
    return false;
  }
  gneiss::engine::prefab_asset_ref failed = first;
  if (assets.store<gneiss::engine::prefab_description>(
          "asset://unmounted/lamp.prefab.json",
          std::make_shared<const gneiss::engine::prefab_description>(description),
          failed) != gneiss::engine::result::not_found ||
      failed.get() != first.get()) {
    return false;
  }
  if (assets.store<gneiss::engine::prefab_description>(uri, {}, failed) !=
          gneiss::engine::result::invalid_argument ||
      failed.get() != first.get()) {
    return false;
  }
  // 当前缓存无人使用即可回收，旧实例仍持有自身的不可变结构描述。
  changed = {};
  foreign = {};
  reused = {};
  ordinary = {};
  legacy = {};
  cache.release_unused();
  return cache.size() == 0U && first.get()->objects[0].translation[0] == 1.0F;
}

int main() try {
  // 反射 codec 保留未知字段；解码失败不覆盖旧对象。
  {
    namespace e = gneiss::engine;
    e::registry_ref registry;
    if (e::builtin_resource_registry(registry).failed())
      return 90;
    auto text = std::string(valid_prefab);
    text.insert(1, R"("codec_unknown":{"keep":7},)");
    const auto source = std::as_bytes(std::span{text.data(), text.size()});
    e::prefab_description value;
    e::serialization_diagnostic detail;
    std::vector<std::byte> encoded;
    if (e::deserialize(registry, source, value, detail).failed() ||
        e::serialize(registry, value, encoded, detail).failed())
      return 91;
    e::prefab_description restored;
    if (e::deserialize(registry, encoded, restored, detail).failed() ||
        restored.objects.size() != value.objects.size() ||
        restored.author_json.find("codec_unknown") == std::string::npos)
      return 92;
    const auto saved = restored.author_json;
    const std::string broken = "{";
    if (e::deserialize(registry, std::as_bytes(std::span{broken.data(), broken.size()}), restored,
                       detail)
            .ok() ||
        restored.author_json != saved)
      return 93;
    std::vector<std::byte> oversized(e::prefab_description::maximum_source_bytes + 1U);
    if (e::deserialize(registry, oversized, restored, detail) != e::result::invalid_argument ||
        restored.author_json != saved)
      return 94;
  }

  if (!prepared_description_contract()) {
    return 9;
  }
  gneiss::engine::prefab_description prefab;
  gneiss::engine::scene_diagnostic diagnostic;
  if (gneiss::engine::parse_prefab_description(valid_prefab, prefab, diagnostic) !=
          gneiss::engine::result::success ||
      prefab.source_schema_version != 1U || prefab.objects.size() != 2U ||
      prefab.objects[1].parent_uuid != prefab.objects[0].uuid || prefab.dependencies.size() != 2U ||
      prefab.dependencies[0] != "asset://materials/lantern.material" ||
      prefab.dependencies[1] != "asset://models/lantern.mesh" ||
      prefab.author_json != valid_prefab) {
    return 1;
  }
  std::string serialized;
  gneiss::engine::prefab_description round_trip;
  const auto author_with_unknown = replace_once(std::string(valid_prefab), R"("version":1,)",
                                                R"("version":1,"editor_unknown":{"kept":true},)");
  if (gneiss::engine::parse_prefab_description(author_with_unknown, prefab, diagnostic) !=
      gneiss::engine::result::success) {
    return 7;
  }
  prefab.objects[1].name = "Serialized Child";
  if (gneiss::engine::serialize_prefab_description(prefab, serialized) !=
          gneiss::engine::result::success ||
      serialized.find("Serialized Child") == std::string::npos ||
      serialized.find("editor_unknown") == std::string::npos ||
      serialized.find("prefab_instances") != std::string::npos ||
      gneiss::engine::parse_prefab_description(serialized, round_trip, diagnostic) !=
          gneiss::engine::result::success ||
      round_trip.objects[1].name != "Serialized Child") {
    return 8;
  }

  const gneiss::engine::prefab_author_address address{.instance_uuid =
                                                          "20000000-0000-4000-8000-000000000001",
                                                      .source_node_uuid = prefab.objects[1].uuid};
  const gneiss::engine::prefab_author_address different_instance{
      .instance_uuid = "20000000-0000-4000-8000-000000000002",
      .source_node_uuid = prefab.objects[1].uuid};
  if (!gneiss::engine::is_valid_prefab_author_address(address) || address == different_instance) {
    return 2;
  }

  const auto multiple_roots =
      replace_once(std::string(valid_prefab), R"("parent":"10000000-0000-4000-8000-000000000002")",
                   R"("parent":null)");
  if (gneiss::engine::parse_prefab_description(multiple_roots, prefab, diagnostic) !=
          gneiss::engine::result::invalid_argument ||
      diagnostic.path != "/objects" || !prefab.objects.empty()) {
    return 3;
  }

  const auto future = replace_once(std::string(valid_prefab), "\"version\":1", "\"version\":2");
  if (gneiss::engine::parse_prefab_description(future, prefab, diagnostic) !=
          gneiss::engine::result::unsupported ||
      diagnostic.path != "/version") {
    return 4;
  }

  // 实例根作为源根的父节点，沿用 Scene Tree 的父子变换组合约定。
  gneiss::engine::scene_tree tree(1U);
  gneiss::scene_node_id instance_root = gneiss::scene_node_id{};
  gneiss::scene_node_id source_root = gneiss::null_scene_node_id;
  gneiss::engine::transform instance_transform = gneiss::engine::transform{};
  instance_transform.translation[0] = 10.0F;
  instance_transform.scale[0] = 2.0F;
  gneiss::engine::transform source_transform = gneiss::engine::transform{};
  source_transform.translation[0] = 3.0F;
  if (tree.create(gneiss::null_scene_node_id, UINT64_C(0), &instance_root) !=
          gneiss::engine::result::success ||
      tree.create(instance_root, UINT64_C(0), &source_root) != gneiss::engine::result::success ||
      tree.set_local(instance_root, instance_transform) != gneiss::engine::result::success ||
      tree.set_local(source_root, source_transform) != gneiss::engine::result::success) {
    return 5;
  }
  gneiss::engine::transform world = gneiss::engine::transform{};
  if (tree.get_world(source_root, &world) != gneiss::engine::result::success ||
      world.translation[0] != 16.0F || world.scale[0] != 2.0F) {
    return 6;
  }

  auto mounted = std::make_shared<memory_file_system>(std::string(valid_prefab));
  gneiss::engine::virtual_file_system file_system;
  if (file_system.mount("asset://prefabs/", mounted).failed())
    return 7;
  gneiss::engine::asset_manager assets(file_system);
  auto& cache = assets.cache();
  gneiss::engine::prefab_asset_ref first;
  gneiss::engine::prefab_asset_ref second;
  if (assets.load_sync("asset://prefabs/lamp.prefab.json", first) !=
          gneiss::engine::result::success ||
      assets.load_sync("asset://prefabs/lamp.prefab.json", second) !=
          gneiss::engine::result::success ||
      first.get() == nullptr || first.get() != second.get() || mounted->read_count != 1U ||
      cache.size() != 1U) {
    return 7;
  }
  gneiss::engine::prefab_asset_ref reloaded;
  if (assets.load_sync("asset://prefabs/lamp.prefab.json", reloaded, true) !=
          gneiss::engine::result::success ||
      !reloaded || reloaded.get() == first.get() || mounted->read_count != 2U ||
      cache.size() != 1U) {
    return 7;
  }
  gneiss::engine::prefab_asset_ref invalid;
  if (assets.load_sync("asset://prefabs/../lamp.prefab.json", invalid) !=
          gneiss::engine::result::invalid_argument ||
      invalid) {
    return 8;
  }
  return 0;
} catch (...) {
  return 99;
}
