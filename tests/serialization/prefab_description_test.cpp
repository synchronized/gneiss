// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/file_system.hpp"
#include "engine/asset/prefab_description.hpp"
#include "engine/asset/resource_cache.hpp"
#include "engine/asset/virtual_file_system.hpp"
#include "engine/function/scene/prefab_asset_loader.hpp"
#include "engine/function/scene/scene_tree.hpp"

#include <gneiss/engine/core/result.h>

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

class memory_file_system final : public gneiss::asset_internal::file_system {
public:
  explicit memory_file_system(std::string content) : content_(std::move(content)) {}

  [[nodiscard]] gneiss_result read(std::string_view path,
                                   std::vector<std::byte>& out_bytes) const noexcept override {
    if (path != "lamp.prefab.json") {
      return GNEISS_ERROR_NOT_FOUND;
    }
    try {
      ++read_count;
      out_bytes.resize(content_.size());
      for (std::size_t index = 0; index < content_.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content_[index]);
      }
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }

  mutable std::size_t read_count = 0;

private:
  std::string content_;
};

} // namespace

bool prepared_versions_contract() {
  using namespace gneiss::asset_internal;
  using namespace gneiss::scene_internal;
  resource_cache cache;
  virtual_file_system files;
  auto backend = std::make_shared<memory_file_system>(std::string(valid_prefab));
  if (files.mount("asset://prefabs/", backend) != GNEISS_SUCCESS) {
    return false;
  }
  const auto same_source = files;
  virtual_file_system different_source;
  if (different_source.mount("asset://prefabs/", backend) != GNEISS_SUCCESS) {
    return false;
  }
  prefab_asset_loader loader(files, cache);
  prefab_asset_loader copied(same_source, cache);
  prefab_asset_loader other(different_source, cache);
  constexpr std::string_view uri = "asset://prefabs/lamp.prefab.json";
  prefab_description description;
  scene_diagnostic diagnostic;
  if (parse_prefab_description(valid_prefab, description, diagnostic) != GNEISS_SUCCESS) {
    return false;
  }
  prefab_asset_lease legacy;
  if (loader.acquire(uri, legacy, diagnostic) != GNEISS_SUCCESS) {
    return false;
  }
  prefab_asset_lease first, reused, changed, foreign;
  if (loader.install_prepared(uri, description, first) != GNEISS_SUCCESS ||
      copied.install_prepared(uri, description, reused) != GNEISS_SUCCESS ||
      first.get() != reused.get() || first.get() == legacy.get() || backend->read_count != 1U) {
    return false;
  }
  // author_json 未变但结构已变时必须产生新版本，不能只摘要原始 JSON。
  description.objects[0].translation[0] = 37.0F;
  if (loader.install_prepared(uri, description, changed) != GNEISS_SUCCESS ||
      changed.get() == first.get() || changed.get()->objects[0].translation[0] != 37.0F ||
      first.get()->objects[0].translation[0] != 1.0F ||
      other.install_prepared(uri, description, foreign) != GNEISS_SUCCESS ||
      foreign.get() == changed.get() || backend->read_count != 1U) {
    return false;
  }
  prefab_asset_lease ordinary;
  if (loader.acquire(uri, ordinary, diagnostic) != GNEISS_SUCCESS ||
      ordinary.get() != legacy.get()) {
    return false;
  }
  prefab_asset_lease failed = first;
  if (loader.install_prepared("asset://unmounted/lamp.prefab.json", description, failed) !=
          GNEISS_ERROR_NOT_FOUND ||
      failed) {
    return false;
  }
  description.objects.clear();
  if (loader.install_prepared(uri, description, failed) != GNEISS_ERROR_INVALID_ARGUMENT ||
      failed) {
    return false;
  }
  // 候选租约释放后可回收，不影响仍被场景/历史持有的旧版本。
  changed = {};
  foreign = {};
  reused = {};
  ordinary = {};
  legacy = {};
  cache.release_unused();
  return cache.size() == 1U && first.get()->objects[0].translation[0] == 1.0F;
}

int main() try {
  if (!prepared_versions_contract()) {
    return 9;
  }
  gneiss::scene_internal::prefab_description prefab;
  gneiss::scene_internal::scene_diagnostic diagnostic;
  if (gneiss::scene_internal::parse_prefab_description(valid_prefab, prefab, diagnostic) !=
          GNEISS_SUCCESS ||
      prefab.source_schema_version != 1U || prefab.objects.size() != 2U ||
      prefab.objects[1].parent_uuid != prefab.objects[0].uuid || prefab.dependencies.size() != 2U ||
      prefab.dependencies[0] != "asset://materials/lantern.material" ||
      prefab.dependencies[1] != "asset://models/lantern.mesh" ||
      prefab.author_json != valid_prefab) {
    return 1;
  }
  std::string serialized;
  gneiss::scene_internal::prefab_description round_trip;
  const auto author_with_unknown = replace_once(std::string(valid_prefab), R"("version":1,)",
                                                R"("version":1,"editor_unknown":{"kept":true},)");
  if (gneiss::scene_internal::parse_prefab_description(author_with_unknown, prefab, diagnostic) !=
      GNEISS_SUCCESS) {
    return 7;
  }
  prefab.objects[1].name = "Serialized Child";
  if (gneiss::scene_internal::serialize_prefab_description(prefab, serialized) != GNEISS_SUCCESS ||
      serialized.find("Serialized Child") == std::string::npos ||
      serialized.find("editor_unknown") == std::string::npos ||
      serialized.find("prefab_instances") != std::string::npos ||
      gneiss::scene_internal::parse_prefab_description(serialized, round_trip, diagnostic) !=
          GNEISS_SUCCESS ||
      round_trip.objects[1].name != "Serialized Child") {
    return 8;
  }

  const gneiss::scene_internal::prefab_author_address address{
      .instance_uuid = "20000000-0000-4000-8000-000000000001",
      .source_node_uuid = prefab.objects[1].uuid};
  const gneiss::scene_internal::prefab_author_address different_instance{
      .instance_uuid = "20000000-0000-4000-8000-000000000002",
      .source_node_uuid = prefab.objects[1].uuid};
  if (!gneiss::scene_internal::is_valid_prefab_author_address(address) ||
      address == different_instance) {
    return 2;
  }

  const auto multiple_roots =
      replace_once(std::string(valid_prefab), R"("parent":"10000000-0000-4000-8000-000000000002")",
                   R"("parent":null)");
  if (gneiss::scene_internal::parse_prefab_description(multiple_roots, prefab, diagnostic) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      diagnostic.path != "/objects" || !prefab.objects.empty()) {
    return 3;
  }

  const auto future = replace_once(std::string(valid_prefab), "\"version\":1", "\"version\":2");
  if (gneiss::scene_internal::parse_prefab_description(future, prefab, diagnostic) !=
          GNEISS_ERROR_UNSUPPORTED ||
      diagnostic.path != "/version") {
    return 4;
  }

  // 实例根作为源根的父节点，沿用 Scene Tree 的父子变换组合约定。
  gneiss::scene_internal::scene_tree tree(1U);
  gneiss_scene_node_id instance_root = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id source_root = GNEISS_NULL_SCENE_NODE_ID;
  gneiss::engine::transform instance_transform = gneiss::engine::transform{};
  instance_transform.translation[0] = 10.0F;
  instance_transform.scale[0] = 2.0F;
  gneiss::engine::transform source_transform = gneiss::engine::transform{};
  source_transform.translation[0] = 3.0F;
  if (tree.create(GNEISS_NULL_SCENE_NODE_ID, GNEISS_NULL_ENTITY_ID, &instance_root) !=
          GNEISS_SUCCESS ||
      tree.create(instance_root, GNEISS_NULL_ENTITY_ID, &source_root) != GNEISS_SUCCESS ||
      tree.set_local(instance_root, instance_transform) != GNEISS_SUCCESS ||
      tree.set_local(source_root, source_transform) != GNEISS_SUCCESS) {
    return 5;
  }
  gneiss::engine::transform world = gneiss::engine::transform{};
  if (tree.get_world(source_root, &world) != GNEISS_SUCCESS || world.translation[0] != 16.0F ||
      world.scale[0] != 2.0F) {
    return 6;
  }

  auto mounted = std::make_shared<memory_file_system>(std::string(valid_prefab));
  gneiss::asset_internal::virtual_file_system file_system;
  gneiss::asset_internal::resource_cache cache;
  gneiss::scene_internal::prefab_asset_loader loader(file_system, cache);
  gneiss::scene_internal::prefab_asset_lease first;
  gneiss::scene_internal::prefab_asset_lease second;
  if (file_system.mount("asset://prefabs/", mounted) != GNEISS_SUCCESS ||
      loader.acquire("asset://prefabs/lamp.prefab.json", first, diagnostic) != GNEISS_SUCCESS ||
      loader.acquire("asset://prefabs/lamp.prefab.json", second, diagnostic) != GNEISS_SUCCESS ||
      first.get() == nullptr || first.get() != second.get() || mounted->read_count != 1U ||
      cache.size() != 1U) {
    return 7;
  }
  gneiss::scene_internal::prefab_asset_lease reloaded;
  if (loader.reload("asset://prefabs/lamp.prefab.json", reloaded, diagnostic) != GNEISS_SUCCESS ||
      !reloaded || reloaded.get() == first.get() || mounted->read_count != 2U ||
      cache.size() != 1U) {
    return 7;
  }
  gneiss::scene_internal::prefab_asset_lease invalid;
  if (loader.acquire("asset://prefabs/../lamp.prefab.json", invalid, diagnostic) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      invalid || diagnostic.message.empty()) {
    return 8;
  }
  return 0;
} catch (...) {
  return 99;
}
