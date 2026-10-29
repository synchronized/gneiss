// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/resource_cache.hpp"
#include "engine/function/scene/scene_tree.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/scene/prefab_codec.hpp"
#include "engine/resource/scene/prefab_description.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view valid_prefab = R"({
  "format":"gneiss.prefab",
  "version":1,
  "prefab_uuid":"10000000-0000-4000-8000-000000000001","root_uuid":"10000000-0000-4000-8000-000000000002",
  "objects":[
    {
      "uuid":"10000000-0000-4000-8000-000000000002",
      "name":"Prefab Root",
      "parent":null,
      "transform":{"translation":[1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://models/lantern.mesh","material":"asset://materials/lantern.material"}}]
    },
    {
      "uuid":"10000000-0000-4000-8000-000000000003",
      "name":"Child",
      "parent":"10000000-0000-4000-8000-000000000002",
      "transform":{"translation":[0,1,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":[]
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

class memory_file_system final : public gneiss::test_support::memory_file_fixture {
public:
  explicit memory_file_system(std::string content) : content_(std::move(content)) {}

  [[nodiscard]] gneiss::engine::error
  read(std::string_view path, std::vector<std::byte>& out_bytes) const noexcept override {
    if (path != "lamp.prefab.json") {
      return gneiss::engine::error::not_found;
    }
    try {
      ++read_count;
      out_bytes.resize(content_.size());
      for (std::size_t index = 0; index < content_.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content_[index]);
      }
      return gneiss::engine::error::success;
    } catch (...) {
      return gneiss::engine::error::out_of_memory;
    }
  }

  mutable std::size_t read_count = 0;

  gneiss::engine::error read_bounded(std::string_view path, std::size_t limit,
                                     std::vector<std::byte>& output) const noexcept override {
    if (content_.size() > limit)
      return gneiss::engine::error::invalid_argument;
    return read(path, output);
  }

private:
  std::string content_;
};

} // namespace

bool prepared_description_contract() {
  using namespace gneiss::engine;
  using namespace gneiss::engine;

  gneiss::test_support::fixture_files files;
  auto backend = std::make_shared<memory_file_system>(std::string(valid_prefab));
  if (files.mount("asset://prefabs/", backend) != gneiss::engine::error::success) {
    return false;
  }

  gneiss::test_support::fixture_files different_source;
  if (different_source.mount("asset://prefabs/", backend) != gneiss::engine::error::success) {
    return false;
  }
  asset_manager assets(gneiss::test_support::test_files(files));
  auto& cache = assets.cache();
  asset_manager other_assets(gneiss::test_support::test_files(different_source));
  constexpr std::string_view uri = "asset://prefabs/lamp.prefab.json";
  gneiss::engine::prefab_description description;
  gneiss::engine::scene_diagnostic diagnostic;
  if (gneiss::test_support::assign_decoded(gneiss::engine::parse_prefab_description(valid_prefab),
                                           description,
                                           diagnostic) != gneiss::engine::error::success) {
    return false;
  }
  gneiss::engine::prefab_asset_ref legacy;
  if (gneiss::test_support::load_asset(assets, uri, legacy) != gneiss::engine::error::success) {
    return false;
  }
  gneiss::engine::prefab_asset_ref first, reused, changed, foreign;
  if (gneiss::test_support::assign_value(
          assets.store<gneiss::engine::prefab_description>(
              uri, std::make_shared<const gneiss::engine::prefab_description>(description)),
          first) != gneiss::engine::error::success ||
      gneiss::test_support::assign_value(
          assets.store<gneiss::engine::prefab_description>(
              uri, std::make_shared<const gneiss::engine::prefab_description>(description)),
          reused) != gneiss::engine::error::success ||
      first.get() == reused.get() || first.get() == legacy.get() || backend->read_count != 1U) {
    return false;
  }
  // 替换当前描述，不改写已实例化场景持有的旧描述。
  description.objects[0].transform.translation[0] = 37.0F;
  if (gneiss::test_support::assign_value(
          assets.store<gneiss::engine::prefab_description>(
              uri, std::make_shared<const gneiss::engine::prefab_description>(description)),
          changed) != gneiss::engine::error::success ||
      changed.get() == first.get() || changed.get()->objects[0].transform.translation[0] != 37.0F ||
      first.get()->objects[0].transform.translation[0] != 1.0F ||
      gneiss::test_support::assign_value(
          other_assets.store<gneiss::engine::prefab_description>(
              uri, std::make_shared<const gneiss::engine::prefab_description>(description)),
          foreign) != gneiss::engine::error::success ||
      foreign.get() == changed.get() || backend->read_count != 1U) {
    return false;
  }
  gneiss::engine::prefab_asset_ref ordinary;
  if (gneiss::test_support::load_asset(assets, uri, ordinary) != gneiss::engine::error::success ||
      ordinary.get() != changed.get()) {
    return false;
  }
  gneiss::engine::prefab_asset_ref failed = first;
  if (gneiss::test_support::assign_value(
          assets.store<gneiss::engine::prefab_description>(
              "asset://unmounted/lamp.prefab.json",
              std::make_shared<const gneiss::engine::prefab_description>(description)),
          failed) != gneiss::engine::error::not_found ||
      failed.get() != first.get()) {
    return false;
  }
  if (gneiss::test_support::assign_value(assets.store<gneiss::engine::prefab_description>(uri, {}),
                                         failed) != gneiss::engine::error::invalid_argument ||
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
  return cache.size() == 0U && first.get()->objects[0].transform.translation[0] == 1.0F;
}

bool explicit_root_contract() {
  namespace e = gneiss::engine;
  auto parsed = e::parse_prefab_description(valid_prefab);
  if (!parsed || parsed->root_uuid != parsed->objects.front().uuid)
    return false;
  const auto root = parsed->root_uuid;
  const auto child = parsed->objects.back().uuid;
  std::swap(parsed->objects[0], parsed->objects[1]);
  auto text = e::serialize_prefab_description(*parsed);
  if (!text)
    return false;
  auto restored = e::parse_prefab_description(*text);
  if (!restored || restored->root_uuid != root || restored->objects[0].uuid != child)
    return false;
  // 根选择写回文件，不能保留 author_json 中的旧 root_uuid。
  parsed->root_uuid = child;
  parsed->objects[0].parent_uuid.reset();
  parsed->objects[1].parent_uuid = child;
  text = e::serialize_prefab_description(*parsed);
  if (!text)
    return false;
  restored = e::parse_prefab_description(*text);
  if (!restored || restored->root_uuid != child)
    return false;
  parsed->root_uuid = root; // 有父节点的对象不能被指定为根。
  if (e::serialize_prefab_description(*parsed))
    return false;
  for (const auto invalid :
       {"00000000-0000-0000-0000-000000000000", "10000000-0000-4000-8000-000000000099",
        "10000000-0000-4000-8000-000000000003", "INVALID"}) {
    auto broken = replace_once(std::string(valid_prefab),
                               R"("root_uuid":"10000000-0000-4000-8000-000000000002")",
                               std::string(R"("root_uuid":")") + invalid + "\"");
    if (e::parse_prefab_description(broken))
      return false;
  }
  auto missing = replace_once(std::string(valid_prefab),
                              R"("root_uuid":"10000000-0000-4000-8000-000000000002",)", "");
  if (e::parse_prefab_description(missing))
    return false;
  const auto cycle = replace_once(std::string(valid_prefab), R"("parent":null)",
                                  R"("parent":"10000000-0000-4000-8000-000000000003")");
  if (e::parse_prefab_description(cycle))
    return false;
  const auto duplicate =
      replace_once(std::string(valid_prefab), R"("uuid":"10000000-0000-4000-8000-000000000003")",
                   R"("uuid":"10000000-0000-4000-8000-000000000002")");
  return !e::parse_prefab_description(duplicate);
}

bool save_tree_contract() {
  namespace e = gneiss::engine;
  auto parsed = e::parse_prefab_description(valid_prefab);
  if (!parsed)
    return false;
  const auto rejects = [&](auto change) {
    auto candidate = *parsed;
    change(candidate);
    auto encoded = e::encode_prefab(candidate);
    return !encoded && encoded.error().error == e::error::invalid_argument;
  };
  return rejects([](auto& prefab) { prefab.root_uuid = {}; }) &&
         rejects([](auto& prefab) { prefab.objects[1].parent_uuid.reset(); }) &&
         rejects([](auto& prefab) { prefab.objects[1].parent_uuid = prefab.objects[1].uuid; }) &&
         rejects([](auto& prefab) { prefab.objects[1].parent_uuid = prefab.uuid; }) &&
         rejects([](auto& prefab) { prefab.objects[1].uuid = prefab.objects[0].uuid; });
}

int main() try {
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  const auto nested = replace_once(std::string(valid_prefab), R"("components":[])",
                                   R"("prefab":{"uri":"asset://nested.prefab","overrides":[]})");
  const auto parsed_nested = gneiss::engine::parse_prefab_description(nested);
  if (parsed_nested || parsed_nested.error().error != gneiss::engine::error::unsupported)
    return 95;
  // 反射 codec 保留未知字段；解码失败不覆盖旧对象。
  {
    namespace e = gneiss::engine;
    gneiss::engine::meta::registry_ref registry;
    if (gneiss::test_support::assign_value(e::meta::default_registry(), registry).failed())
      return 90;
    auto text = std::string(valid_prefab);
    text.insert(1, R"("codec_unknown":{"keep":7},)");
    const auto source = std::as_bytes(std::span{text.data(), text.size()});
    e::prefab_description value;
    gneiss::engine::meta::serialization_diagnostic detail;
    std::vector<std::byte> encoded;
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(value)>>(registry,
                                                                                    source),
            value, detail)
            .failed() ||
        gneiss::test_support::assign_decoded(gneiss::engine::meta::serialize(registry, value),
                                             encoded, detail)
            .failed())
      return 91;
    e::prefab_description restored;
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(restored)>>(registry,
                                                                                       encoded),
            restored, detail)
            .failed() ||
        restored.objects.size() != value.objects.size() ||
        restored.author_json.find("codec_unknown") == std::string::npos)
      return 92;
    const auto saved = restored.author_json;
    const std::string broken = "{";
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(restored)>>(
                registry, std::as_bytes(std::span{broken.data(), broken.size()})),
            restored, detail)
            .ok() ||
        restored.author_json != saved)
      return 93;
    std::vector<std::byte> oversized(e::prefab_description::maximum_source_bytes + 1U);
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(restored)>>(registry,
                                                                                       oversized),
            restored, detail) != e::error::invalid_argument ||
        restored.author_json != saved)
      return 94;
  }

  if (!save_tree_contract())
    return 96;
  if (!explicit_root_contract())
    return 95;
  if (!prepared_description_contract()) {
    return 9;
  }
  gneiss::engine::prefab_description prefab;
  gneiss::engine::scene_diagnostic diagnostic;
  if (gneiss::test_support::assign_decoded(gneiss::engine::parse_prefab_description(valid_prefab),
                                           prefab, diagnostic) != gneiss::engine::error::success ||
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
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::parse_prefab_description(author_with_unknown), prefab, diagnostic) !=
      gneiss::engine::error::success) {
    return 7;
  }
  prefab.objects[1].name = "Serialized Child";
  if (gneiss::test_support::assign_value(gneiss::engine::serialize_prefab_description(prefab),
                                         serialized) != gneiss::engine::error::success ||
      serialized.find("Serialized Child") == std::string::npos ||
      serialized.find("editor_unknown") == std::string::npos ||
      serialized.find("prefab_instances") != std::string::npos ||
      gneiss::test_support::assign_decoded(gneiss::engine::parse_prefab_description(serialized),
                                           round_trip,
                                           diagnostic) != gneiss::engine::error::success ||
      round_trip.objects[1].name != "Serialized Child") {
    return 8;
  }

  // 新建描述不依赖原文件；非法根节点仍须拒绝保存。
  auto fresh = prefab;
  fresh.author_json.clear();
  auto fresh_bytes = gneiss::engine::encode_prefab(fresh);
  if (!fresh_bytes)
    return 40;
  auto fresh_copy = gneiss::engine::decode_prefab(*fresh_bytes);
  if (!fresh_copy || fresh_copy->root_uuid != fresh.root_uuid ||
      fresh_copy->objects.size() != fresh.objects.size())
    return 41;
  fresh.root_uuid = {};
  if (gneiss::engine::encode_prefab(fresh))
    return 42;
  const gneiss::engine::prefab_author_address address{
      .instance_uuid = *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000001"),
      .source_node_uuid = prefab.objects[1].uuid};
  const gneiss::engine::prefab_author_address different_instance{
      .instance_uuid = *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000002"),
      .source_node_uuid = prefab.objects[1].uuid};
  if (!gneiss::engine::is_valid_prefab_author_address(address) || address == different_instance) {
    return 2;
  }

  const auto multiple_roots =
      replace_once(std::string(valid_prefab), R"("parent":"10000000-0000-4000-8000-000000000002")",
                   R"("parent":null)");
  auto rejected = gneiss::engine::parse_prefab_description(multiple_roots);
  if (rejected || rejected.error().error != gneiss::engine::error::invalid_argument ||
      rejected.error().path != "/objects") {
    return 3;
  }

  const auto future = replace_once(std::string(valid_prefab), "\"version\":1", "\"version\":2");
  if (gneiss::test_support::assign_decoded(gneiss::engine::parse_prefab_description(future), prefab,
                                           diagnostic) != gneiss::engine::error::unsupported ||
      diagnostic.path != "/version") {
    return 4;
  }

  // 实例根作为源根的父节点，沿用 Scene Tree 的父子变换组合约定。
  gneiss::engine::scene_tree tree(1U);
  gneiss::scene_node_id instance_root = gneiss::scene_node_id{};
  gneiss::scene_node_id source_root = gneiss::null_scene_node_id;
  gneiss::engine::math::transform instance_transform = gneiss::engine::math::transform{};
  instance_transform.translation[0] = 10.0F;
  instance_transform.scale[0] = 2.0F;
  gneiss::engine::math::transform source_transform = gneiss::engine::math::transform{};
  source_transform.translation[0] = 3.0F;
  if (gneiss::test_support::assign_value(tree.create(gneiss::null_scene_node_id, UINT64_C(0)),
                                         instance_root) != gneiss::engine::error::success ||
      gneiss::test_support::assign_value(tree.create(instance_root, UINT64_C(0)), source_root) !=
          gneiss::engine::error::success ||
      tree.set_local(instance_root, instance_transform) != gneiss::engine::error::success ||
      tree.set_local(source_root, source_transform) != gneiss::engine::error::success) {
    return 5;
  }
  gneiss::engine::math::transform world = gneiss::engine::math::transform{};
  if (gneiss::test_support::assign_value(tree.get_world(source_root), world) !=
          gneiss::engine::error::success ||
      world.translation[0] != 16.0F || world.scale[0] != 2.0F) {
    return 6;
  }

  auto mounted = std::make_shared<memory_file_system>(std::string(valid_prefab));
  gneiss::test_support::fixture_files file_system;
  if (file_system.mount("asset://prefabs/", mounted).failed())
    return 7;
  gneiss::engine::asset_manager assets(gneiss::test_support::test_files(file_system));
  auto& cache = assets.cache();
  gneiss::engine::prefab_asset_ref first;
  gneiss::engine::prefab_asset_ref second;
  if (gneiss::test_support::load_asset(assets, "asset://prefabs/lamp.prefab.json", first) !=
          gneiss::engine::error::success ||
      gneiss::test_support::load_asset(assets, "asset://prefabs/lamp.prefab.json", second) !=
          gneiss::engine::error::success ||
      first.get() == nullptr || first.get() != second.get() || mounted->read_count != 1U ||
      cache.size() != 1U) {
    return 7;
  }
  gneiss::engine::prefab_asset_ref reloaded;
  if (gneiss::test_support::load_asset(assets, "asset://prefabs/lamp.prefab.json", reloaded,
                                       true) != gneiss::engine::error::success ||
      !reloaded || reloaded.get() == first.get() || mounted->read_count != 2U ||
      cache.size() != 1U) {
    return 7;
  }
  gneiss::engine::prefab_asset_ref invalid;
  if (gneiss::test_support::load_asset(assets, "asset://prefabs/../lamp.prefab.json", invalid) !=
          gneiss::engine::error::invalid_argument ||
      invalid) {
    return 8;
  }
  return 0;
} catch (...) {
  return 99;
}
