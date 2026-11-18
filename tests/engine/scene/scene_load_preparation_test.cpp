// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_asset.hpp"
#include "../../support/async_scene.hpp"
#include "../../support/memory_file_fixture.hpp"
#include "../../support/scene_application.hpp"
#include "../../support/value_result.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include "function/asset/detail/content_dependency.hpp"
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/files/service.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/prefab.hpp>
#include <gneiss/engine/resource/scene.hpp>
#include <gneiss/engine/resource/scene/prefab_description.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
using namespace gneiss;

void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("场景准备契约失败，行=" + std::to_string(where.line()));
  }
}

struct memory_files final : gneiss::test_support::memory_file_fixture {
  std::map<std::string, std::string> files;
  mutable std::map<std::string, unsigned> reads;
  bool change_on_verify{};
  gneiss::error read(std::string_view, std::vector<std::byte>&) const noexcept override {
    return gneiss::error::unsupported;
  }
  gneiss::error read_bounded(std::string_view path, std::size_t limit,
                                     std::vector<std::byte>& bytes) const noexcept override try {
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::error::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::error::invalid_argument;
    }
    bytes.resize(found->second.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(found->second[i]);
    }
    if (++reads[std::string(path)] == 2U && change_on_verify && !bytes.empty()) {
      bytes.back() = std::byte{' '};
    }
    return gneiss::error::success;
  } catch (...) {
    return gneiss::error::out_of_memory;
  }
};

constexpr std::string_view object_json = R"({"uuid":"00000000-0000-4000-8000-000000000002",
"parent":{"object":null,"source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://m.mesh","material":"asset://m.material","uuid":"ccb2708e-09e4-4d1c-86c6-8ff0f3c19cf4"}}]})";
constexpr std::string_view scene_prefix = R"({"format":"gneiss.scene","version":5,
"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[)";

std::string instance(std::string_view id) {
  return R"({"uuid":"00000000-0000-4000-8000-00000000000)" + std::string(id) +
         R"(","prefab":{"uri":"asset://p.prefab","overrides":[]},"parent":{"object":null,"source":null},
"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}})";
}

unsigned scene_decodes{}, prefab_decodes{};
result<scene_description, serialization::diagnostic>
custom_scene(std::span<const std::byte> bytes) {
  ++scene_decodes;
  return gneiss::asset_file::deserialize<scene_description>(bytes);
}
result<prefab_description, serialization::diagnostic>
custom_prefab(std::span<const std::byte> bytes) {
  ++prefab_decodes;
  return gneiss::asset_file::deserialize<prefab_description>(bytes);
}
struct linked_asset final : asset {
  ref<const linked_asset> child;
  explicit linked_asset(asset_identity source) : asset(runtime_type_of<linked_asset>()) {
    identity_ = std::move(source);
  }
};
struct linked_loader final : asset_loader {
  asset_identity source;
  std::shared_ptr<detail::content_dependency<linked_asset, int>> child;
  explicit linked_loader(asset_identity identity) : asset_loader(identity.uri), source(identity) {}
  void decode(const std::vector<std::byte>& bytes) override {
    value = std::make_shared<linked_asset>(source);
    if (!bytes.empty()) {
      std::string child_uri(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      child = std::make_shared<detail::content_dependency<linked_asset, int>>(
          child_uri, false, [](const linked_asset&) { return 1; });
      child->path = "/child";
      dependencies.push_back(child);
    }
    status = error::success;
  }
  error finish() override {
    if (child)
      std::const_pointer_cast<linked_asset>(std::static_pointer_cast<const linked_asset>(value))
          ->child = child->resource();
    return error::success;
  }
};
void dependency_cycles() {
  auto memory = std::make_shared<memory_files>();
  memory->files = {{"a", "asset://b"},
                   {"b", "asset://a"},
                   {"self", "asset://self"},
                   {"leaf", ""},
                   {"root", "asset://leaf"}};
  test_support::fixture_files files;
  // 单在途文件槽验证多个根及其依赖遇到背压时继续排队，不把未提交请求误当已启动。
  files.service =
      std::make_shared<gneiss::files::service>(gneiss::files::service_options{.max_requests = 1});
  check(files.mount("asset://", memory).ok());
  asset_manager assets(test_support::test_files(files));
  auto& executor = test_support::asset_tasks();
  check(assets.configure(executor).ok());
  check(assets
            .register_loader<linked_asset>([](auto source, auto) {
              return std::make_shared<linked_loader>(std::move(source));
            })
            .ok());
  auto a = assets.load<linked_asset>("asset://a");
  auto b = assets.load<linked_asset>("asset://b");
  auto self = assets.load<linked_asset>("asset://self");
  auto root = assets.load<linked_asset>("asset://root");
  check(bool(a) && bool(b) && bool(self) && bool(root));
  for (unsigned i = 0; i < 100U; ++i) {
    check(assets.async_files()->dispatch_completions(64).ok());
    assets.advance();
    check(executor.run_ready().status == gneiss::tasks::drive_status::success);
  }
  check(a->status() == error::invalid_state && b->status() == error::invalid_state);
  check(self->status() == error::invalid_state && root->status().ok());
  check(a->diagnostic().message.find("asset://a") != std::string::npos);
  check(a->diagnostic().message.find("asset://b") != std::string::npos);
  check(a->diagnostic().path.find("child") != std::string::npos);
  auto loaded = root->take();
  check(bool(loaded) && bool((*loaded)->child));
  check(memory->reads["leaf"] == 1U);
  // 成功内容的拥有边也参与检测，不能通过独立重载构造强引用环。
  memory->files["leaf"] = "asset://root";
  auto cycle_reload = assets.reload<linked_asset>("asset://leaf");
  check(bool(cycle_reload));
  for (unsigned i = 0; i < 100U && cycle_reload->status() == error::not_ready; ++i) {
    check(assets.async_files()->dispatch_completions(64).ok());
    assets.advance();
    check(executor.run_ready().status == gneiss::tasks::drive_status::success);
  }
  check(cycle_reload->status() == error::invalid_state);
  check(!(*loaded)->child->child);
  cycle_reload->cancel();
  a->cancel();
  b->cancel();
  self->cancel();
  loaded->reset();
  assets.collect_unused();
  check(!assets.find<linked_asset>("asset://root"));
}
void run() {
  auto memory = std::make_shared<memory_files>();
  memory->files["main.scene"] = std::string(scene_prefix) + std::string(object_json) + "," +
                                instance("3") + "," + instance("4") + "]}";
  memory->files["p.prefab"] =
      R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"00000000-0000-4000-8000-000000000005","root_uuid":"00000000-0000-4000-8000-000000000002","objects":[)" +
      std::string(object_json) + "]}";
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", memory) == gneiss::error::success);
  auto& executor = gneiss::test_support::asset_tasks();
  asset_manager assets(gneiss::test_support::test_files(files));
  check(assets.register_decoder<scene_description>(&custom_scene) == error::unsupported);
  check(assets.register_decoder<prefab_description>(&custom_prefab) == error::unsupported);
  assets.configure(executor);
  // 普通场景请求共享工作；取消一个观察者不影响另一个。内容快照独立保活依赖。
  {
    asset_manager independent(test_support::test_files(files));
    check(independent.configure(executor).ok());
    auto first = independent.load<gneiss::scene>("asset://main.scene");
    auto second = independent.load<gneiss::scene>("asset://main.scene");
    check(bool(first) && bool(second));
    check(first->resource() == second->resource() && bool(first->resource()));
    check(!first->resource()->content());
    first->cancel();
    for (unsigned i = 0; i < 100U && second->status() == error::not_ready; ++i) {
      check(independent.async_files()->dispatch_completions(64).ok());
      independent.advance();
      check(executor.run_ready().status == gneiss::tasks::drive_status::success);
    }
    auto result = second->take();
    check(bool(result));
    auto content = (*result)->content();
    check(content->prefabs.size() == 1U);
    result->reset();
    // 不清理索引直接复用过期记录，不能继承旧身份的就绪标记。
    auto fresh = independent.load<gneiss::scene>("asset://main.scene");
    check(bool(fresh) && !fresh->resource()->content());
    check(independent.find<gneiss::scene>("asset://main.scene").error() == error::not_ready);
    fresh->cancel();
    independent.advance();
    independent.collect_unused();
    check(!independent.find<gneiss::scene>("asset://main.scene"));
    check(bool(independent.find<gneiss::prefab>("asset://p.prefab")));
    content.reset();
    independent.collect_unused();
    check(!independent.find<gneiss::prefab>("asset://p.prefab"));
  }
  memory->reads.clear();
  scene_content output;
  ref<const gneiss::scene> loaded_identity;
  gneiss::scene_diagnostic diagnostic;
  auto prepare = [&] {
    return gneiss::test_support::load_scene_content(assets, executor, "asset://main.scene", output,
                                                    diagnostic, &loaded_identity);
  };
  check(prepare().ok());
  auto composed = test_support::compose(output);
  check(bool(composed) && composed->objects.size() == 3U);
  auto dependencies = scene_dependencies(*composed);
  check(bool(dependencies) && dependencies->size() == 2U && output.prefabs.size() == 1U);
  check(memory->reads["p.prefab"] == 1U);
  check(bool(loaded_identity) && bool(output.prefabs.at("asset://p.prefab").resource));
  check(assets.find<gneiss::scene>("asset://main.scene")->get() == loaded_identity.get());
  check(assets.find<gneiss::prefab>("asset://p.prefab")->get() ==
        output.prefabs.at("asset://p.prefab").resource.get());
  const auto reads_before_rejection = memory->reads;
  auto old_scene = assets.load<scene_description>("asset://main.scene");
  auto old_prefab = assets.load<prefab_description>("asset://p.prefab");
  check(!old_scene && old_scene.error() == error::unsupported);
  check(!old_prefab && old_prefab.error() == error::unsupported);
  check(memory->reads == reads_before_rejection);
  auto cached_request = assets.load<gneiss::scene>("asset://main.scene");
  check(bool(cached_request));
  auto cached_scene = cached_request->take();
  check(bool(cached_scene) &&
        cached_request->source_bytes() == memory->files.at("main.scene").size());
  // 场景加载本身持有去重后的结构依赖；视觉文件不存在也不影响成功。
  const auto loaded_content = (*cached_scene)->content();
  check(loaded_content->prefabs.size() == 1U);
  check(loaded_content->prefabs.at("asset://p.prefab").resource ==
        output.prefabs.at("asset://p.prefab").resource);
  check(!memory->reads.contains("m.mesh") && !memory->reads.contains("m.material"));
  check(scene_decodes == 0U && prefab_decodes == 0U);
  const auto inputs = *loaded_content;
  // 资产身份稳定；旧快照不随重载改变，失败重载不破坏当前内容。
  ref<const gneiss::scene> scene_asset;
  check(test_support::load_asset(assets, "asset://main.scene", scene_asset).ok());
  check(scene_asset->type() == runtime_type_of<gneiss::scene>());
  check(scene_asset->source().uri == "asset://main.scene" && bool(scene_asset->source().source));
  auto snapshot = scene_asset->snapshot();
  auto fixed_request = assets.load<gneiss::scene>("asset://main.scene");
  check(bool(fixed_request) && fixed_request->status().ok());
  const auto fixed_content = fixed_request->snapshot();
  check(fixed_content == scene_asset->content());
  const auto original_file = memory->files["main.scene"];
  auto edited = *snapshot;
  edited.objects.front().name = "reloaded";
  auto encoded = asset_file::serialize_bytes(edited);
  check(bool(encoded));
  memory->files["main.scene"] =
      std::string(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  ref<const gneiss::scene> reloaded;
  check(test_support::load_asset(assets, "asset://main.scene", reloaded, true).ok());
  check(reloaded == scene_asset && snapshot->objects.front().name != "reloaded");
  check(reloaded->snapshot()->objects.front().name == "reloaded");
  check(fixed_request->snapshot() == fixed_content && fixed_content != reloaded->content());
  check(fixed_content->description->objects.front().name != "reloaded");
  check(bool(fixed_request->take()) && !fixed_request->snapshot());
  reloaded.reset();
  assets.collect_unused();
  auto retained = assets.find<gneiss::scene>("asset://main.scene");
  check(bool(retained) && retained->get() == scene_asset.get());
  memory->files["main.scene"] = "{";
  ref<const gneiss::scene> rejected;
  check(test_support::load_asset(assets, "asset://main.scene", rejected, true).failed());
  check(scene_asset->snapshot()->objects.front().name == "reloaded");
  check(assets.find<gneiss::scene>("asset://main.scene")->get() == scene_asset.get());
  memory->files["main.scene"] = original_file;
  ref<const gneiss::prefab> prefab_asset;
  check(test_support::load_asset(assets, "asset://p.prefab", prefab_asset).ok());
  check(prefab_asset->snapshot()->root_uuid ==
        inputs.prefabs.at("asset://p.prefab").description->root_uuid);
  // 声明依赖没有副作用；通用调度取得固定内容，之后独立重载不改变该结果。
  const auto before_declaration = memory->reads;
  detail::content_dependency<gneiss::prefab, ref<const prefab_description>> dependency(
      "asset://p.prefab", false, [](const gneiss::prefab& value) { return value.snapshot(); });
  check(memory->reads == before_declaration);
  check(dependency.advance(assets).ok() && dependency.resource() == prefab_asset);
  const auto dependency_content = dependency.content();
  ref<const asset> erased = prefab_asset;
  check(asset_cast<gneiss::prefab>(erased) == prefab_asset && !asset_cast<gneiss::scene>(erased));
  const auto reads = memory->reads;
  composed = test_support::compose(inputs);
  check(bool(composed) && memory->reads == reads);
  // 只收集每个实例最终生效的资源；全被覆盖的来源材质不再是加载依赖。
  auto overridden = std::make_shared<scene_description>(*inputs.description);
  std::erase_if(overridden->objects, [](const auto& node) { return !node.prefab; });
  const auto source_uuid = inputs.prefabs.at("asset://p.prefab").description->root_uuid;
  for (auto& node : overridden->objects) {
    node.prefab->overrides.push_back(
        {.key = {.target = {.source_node_uuid = source_uuid,
                            .target_type_guid = mesh_component_description::type_guid,
                            .component_uuid =
                                inputs.prefabs.at("asset://p.prefab")
                                    .description->objects.front()
                                    .components.identity(mesh_component_description::type_guid)},
                 .path = {meta::property_id{2}}},
         .operation = value{std::string("asset://override.material")}});
  }
  auto overridden_inputs = inputs;
  overridden_inputs.description = asset_ref<const scene_description>{overridden};
  composed = test_support::compose(overridden_inputs);
  check(bool(composed));
  dependencies = scene_dependencies(*composed);
  check(bool(dependencies));
  check((*dependencies).size() == 2);
  check(std::ranges::none_of((*dependencies),
                             [](const auto& asset) { return asset.uri == "asset://m.material"; }));
  check(std::ranges::any_of(
      (*dependencies), [](const auto& asset) { return asset.uri == "asset://override.material"; }));
  // 空材质表示清除引用；不产生材质依赖。
  for (auto& node : overridden->objects)
    node.prefab->overrides.front().operation = value{std::string{}};
  composed = test_support::compose(overridden_inputs);
  check(bool(composed));
  dependencies = scene_dependencies(*composed);
  check(bool(dependencies));
  check((*dependencies).size() == 1 && (*dependencies).front().type == render_resource_kind::mesh);
  // 非法覆盖保留为冲突，依赖收集使用来源组件，不加载非法 URI。
  overridden->objects.front().prefab->overrides.front().operation =
      value{std::string{"not-an-asset-uri"}};
  composed = test_support::compose(overridden_inputs);
  check(bool(composed));
  dependencies = scene_dependencies(*composed);
  check(bool(dependencies));
  check(std::ranges::none_of((*dependencies),
                             [](const auto& asset) { return asset.uri == "not-an-asset-uri"; }));
  check(std::ranges::any_of((*dependencies),
                            [](const auto& asset) { return asset.uri == "asset://m.material"; }));
  composed = test_support::compose(inputs);
  check(bool(composed));
  // 依赖已完成但父操作还未领取时，另一轮重载也不能改变这次结果。
  detail::content_dependency<gneiss::prefab, ref<const prefab_description>> delayed_dependency(
      "asset://p.prefab", true, [](const gneiss::prefab& value) { return value.snapshot(); });
  check(delayed_dependency.advance(assets) == error::not_ready);
  ref<const gneiss::prefab> newer;
  check(gneiss::test_support::load_asset(assets, "asset://p.prefab", newer).ok());
  const auto delayed_content = newer->snapshot();
  check(gneiss::test_support::load_asset(assets, "asset://p.prefab", newer, true).ok());
  check(delayed_dependency.advance(assets).ok());
  check(delayed_dependency.content() == delayed_content && delayed_content != newer->snapshot());
  check(newer->snapshot() != inputs.prefabs.at("asset://p.prefab").description);
  check(loaded_content->prefabs.at("asset://p.prefab").description != newer->snapshot());
  check(loaded_content->prefabs.at("asset://p.prefab").resource == newer);
  check(dependency.advance(assets).ok() && dependency.resource() == newer);
  check(dependency.content() == dependency_content && dependency.content() != newer->snapshot());
  const auto before_cached_scene = memory->reads;
  auto cached_structural_request = assets.load<gneiss::scene>("asset://main.scene");
  check(bool(cached_structural_request));
  auto cached_structure = cached_structural_request->take();
  check(bool(cached_structure) && memory->reads == before_cached_scene);
  // 缓存命中保留场景所选择的依赖快照，不自动采用独立重载后的 Prefab。
  check((*cached_structure)->content()->prefabs.at("asset://p.prefab").description !=
        newer->snapshot());
  composed = test_support::compose(inputs);
  check(bool(composed));
  ref<const gneiss::prefab> cached;
  check(gneiss::test_support::load_asset(assets, "asset://p.prefab", cached).ok() &&
        cached == newer);

  // 同一固定内容可以重复组合，不需要文件服务或执行器。
  auto unified = test_support::compose(*scene_asset->content());
  check(bool(unified) && unified->objects.size() == 3U);
  auto repeated = test_support::compose(*scene_asset->content());
  check(bool(repeated) && repeated->objects.size() == 3U);
  check(test_support::compose(scene_content{}).error() == error::invalid_argument);
  auto no_renderer = assets.load<gneiss::texture>("asset://image");
  check(!no_renderer && no_renderer.error() == error::invalid_state);
  auto created = assets.create(*scene_asset->content());
  check(bool(created) && (*created)->source().uri.empty());
  check(!assets.create(scene_description{}));
  memory->reads.clear();
  memory->change_on_verify = true;
  check(prepare() == gneiss::error::success && memory->reads["p.prefab"] == 1U);
  memory->change_on_verify = false;
  memory->files.erase("p.prefab");
  const auto before_failed_reload = scene_asset->content();
  serialization::diagnostic dependency_failure;
  ref<const gneiss::scene> missing_dependency;
  check(test_support::load_asset(assets, "asset://main.scene", missing_dependency, true,
                                 &dependency_failure) == error::not_found);
  check(dependency_failure.path.find("asset://p.prefab") != std::string::npos);
  check(scene_asset->content() == before_failed_reload);
  check(prepare() == gneiss::error::not_found && output.prefabs.empty());

  // 输入顺序不要求父节点优先；准备保留描述顺序，实际应用负责父子创建顺序。
  auto child = std::string(object_json);
  child.replace(child.find("000000000002"), 12U, "000000000003");
  child.replace(child.find("null"), 4U, "\"00000000-0000-4000-8000-000000000002\"");
  memory->files["main.scene"] =
      std::string(scene_prefix) + child + "," + std::string(object_json) + R"(]})";
  check(prepare() == gneiss::error::success && output.description->objects.size() == 2U &&
        output.description->objects[0].parent.object == output.description->objects[1].uuid);
  auto parent = std::string(object_json);
  parent.replace(parent.find("null"), 4U, "\"00000000-0000-4000-8000-000000000003\"");
  memory->files["main.scene"] = std::string(scene_prefix) + child + "," + parent + R"(]})";
  check(prepare() == gneiss::error::invalid_argument && !output.description);
  auto stopping = assets.reload<gneiss::scene>("asset://main.scene");
  check(bool(stopping));
  assets.request_stop();
  check(stopping->take().error().error == error::invalid_state);
  assets.advance();
  check(assets.stopped() && unified->objects.size() == 3U);
  check(assets.reload<gneiss::scene>("asset://main.scene").error() == error::invalid_state);
}
} // namespace

int main() try {
  if (gneiss::initialize_meta().failed())
    return 99;
  run();
  dependency_cycles();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
