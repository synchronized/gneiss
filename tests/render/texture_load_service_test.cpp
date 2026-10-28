// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/memory_file_fixture.hpp"
#include "../support/prepare_render.hpp"
#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/asset_render_state.hpp"
#include "engine/function/resource/detail/render_resource_factory.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/platform/files/file_backend.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/material/material_codec.hpp"
#include "engine/resource/texture/texture_ktx2.hpp"
#include <algorithm>
#include <stdexcept>

#include <atomic>
#include <cstdio>
#include <map>
#include <source_location>

namespace {
using namespace gneiss;
using namespace gneiss::engine;
void check(bool value, std::source_location at = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("异步资产契约失败，行=" + std::to_string(at.line()));
  }
}
struct memory_files final : gneiss::test_support::memory_file_fixture {
  std::map<std::string, std::vector<std::byte>> files;
  mutable std::thread::id reader;
  std::shared_ptr<std::atomic_size_t> bytes_read = std::make_shared<std::atomic_size_t>();
  gneiss::engine::result read(std::string_view path,
                              std::vector<std::byte>& bytes) const noexcept override {
    return read_bounded(path, SIZE_MAX, bytes);
  }
  gneiss::engine::result read_bounded(std::string_view path, std::size_t limit,
                                      std::vector<std::byte>& bytes) const noexcept override {
    reader = std::this_thread::get_id();
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::engine::result::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::engine::result::invalid_argument;
    }
    bytes = found->second;
    bytes_read->fetch_add(bytes.size(), std::memory_order_relaxed);
    return gneiss::engine::result::success;
  }
  void text(std::string path, std::string_view text) {
    auto& bytes = files[std::move(path)];
    bytes.clear();
    for (const auto c : text) {
      bytes.push_back(static_cast<std::byte>(c));
    }
  }
  void pixel(std::byte red) {
    std::string diagnostic;
    check(encode_texture_ktx2(
              {.transfer = texture_transfer::srgb,
               .levels = {{.width = 1U,
                           .height = 1U,
                           .pixels = {red, std::byte{}, std::byte{}, std::byte{255}}}}},
              files["image.ktx2"], diagnostic) == texture_ktx2_result::success);
  }
};
// 人工延迟后端只接受回调；用于验证文件等待与 CPU 调度完全分离。
class delayed_backend final : public gneiss::engine::files::backend {
public:
  std::string path;
  gneiss::engine::files::callback<gneiss::engine::files::bytes> callback;
  std::size_t reads{};
  result read(std::string_view requested, gneiss::engine::files::read_options,
              gneiss::engine::files::callback<gneiss::engine::files::bytes> completed) override {
    check(!callback);
    path = requested;
    callback = std::move(completed);
    ++reads;
    return result::success;
  }
  void complete(gneiss::engine::files::bytes bytes, result status = result::success) {
    auto completed = std::move(callback);
    check(static_cast<bool>(completed));
    completed({.status = status, .data = std::move(bytes)});
  }
};
void prepared_source_revisions() {
  auto source = std::make_shared<memory_files>();
  source->pixel(std::byte{10});
  source->files["payload/image.ktx2"] = source->files.at("image.ktx2");
  source->text(
      "texture",
      R"({"format":"gneiss.texture","version":2,"source":"asset://payload/image.ktx2","color_space":"srgb","usage":"default"})");
  source->text(
      "material",
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":"asset://texture"})");
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::result::success);
  const std::array requests{asset_source{"asset://material", asset_type::material}};
  asset_manager owner(gneiss::test_support::test_files(files));
  prepared_batch before;
  prepared_batch after;
  asset_diagnostic diagnostic;
  check(gneiss::test_support::prepare_render_for_test(owner, files, requests, before, diagnostic,
                                                      {}) == gneiss::engine::result::success);
  prepared_batch repeated;
  check(gneiss::test_support::prepare_render_for_test(owner, files, requests, repeated, diagnostic,
                                                      {}) == gneiss::engine::result::success);
  source->pixel(std::byte{20});
  source->files["payload/image.ktx2"] = source->files.at("image.ktx2");
  check(gneiss::test_support::prepare_render_for_test(owner, files, requests, after, diagnostic,
                                                      {}) == gneiss::engine::result::success);
  check(before.assets.size() == 2U && after.assets.size() == 2U);
  for (std::size_t index = 0; index < before.assets.size(); ++index) {
    const auto& old_asset = before.assets[index];
    const auto& new_asset = after.assets[index];
    check(old_asset.uri == new_asset.uri);
    if (!old_asset.sources.empty()) {
      check(old_asset.sources.front().identity == new_asset.sources.front().identity);
      check(old_asset.sources.front().digest == new_asset.sources.front().digest);
    }
    if (old_asset.kind() == asset_type::texture) {
      check(old_asset.sources.size() == 2U && new_asset.sources.size() == 2U);
      const auto& payload = new_asset.sources.back();
      asset_identity expected;
      check(files.identify("asset://payload/image.ktx2",
                           static_cast<std::uint32_t>(asset_type::texture),
                           expected) == gneiss::engine::result::success);
      check(payload.identity == expected && payload.identity == old_asset.sources.back().identity);
      check(payload.bytes == source->files.at("image.ktx2").size());
      check(payload.digest == gneiss::engine::sha256(source->files.at("image.ktx2")));
      check(payload.digest != old_asset.sources.back().digest);
    } else {
      check(old_asset.sources.empty() && new_asset.sources.empty());
      check(std::get<asset_ref<const material_description>>(new_asset.content)->texture_uris[0] ==
            "asset://texture");
    }
  }
}
void mesh_data_lifetime() {
  auto source = std::make_shared<memory_files>();
  const std::string text =
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[1,0],[0,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  source->text("mesh", text);
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::result::success);
  const std::array requests{asset_source{"asset://mesh", asset_type::mesh}};
  auto prepare = [&](const gneiss::test_support::fixture_files& vfs) {
    prepared_batch batch;
    asset_diagnostic diagnostic;
    asset_manager owner(gneiss::test_support::test_files(vfs));
    check(gneiss::test_support::prepare_render_for_test(owner, vfs, requests, batch, diagnostic,
                                                        {}) == gneiss::engine::result::success);
    return batch;
  };
  auto first = prepare(files);
  const auto* vertices = std::get<mesh_data>(first.assets.front().content).vertices.data();
  auto mesh = std::make_shared<const mesh_data>(
      std::move(std::get<mesh_data>(first.assets.front().content)));
  check(mesh->vertices.data() == vertices);
  render_service resources;
  gneiss::engine::render_resource_factory resources_factory{resources};
  gneiss::engine::rid rid{};
  check(resources.create_prepared_mesh(mesh, &rid) == gneiss::engine::result::success);
  check(resources.share_mesh(rid) == mesh);
  auto frame = resources.share_mesh(rid);
  check(resources.destroy_mesh(rid) == gneiss::engine::result::success &&
        !resources.share_mesh(rid));
  std::weak_ptr<const mesh_data> lifetime = mesh;
  first = {};
  mesh.reset();
  check(!lifetime.expired() && frame->vertices[1].position.x == 1.0F);
  frame.reset();
  check(lifetime.expired());
}
void independent_update_sequences() {
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::engine::result::success);
  render_service resources;
  gneiss::engine::render_resource_factory resources_factory{resources};
  gneiss::engine::asset_manager asset_owner(gneiss::test_support::test_files(files));
  [[maybe_unused]] auto& cache = asset_owner.cache();
  gneiss::engine::resource_service loader{asset_owner};
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  const asset_source source_a{"asset://a", asset_type::material};
  const asset_source source_b{"asset://b", asset_type::material};
  render_asset_ref a, b;
  check(loader.reserve_asset(source_a, a) == gneiss::engine::result::success);
  check(loader.reserve_asset(source_b, b) == gneiss::engine::result::success);
  gneiss::test_support::fixture_files different;
  check(different.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::engine::result::success);
  asset_manager isolated_owner(gneiss::test_support::test_files(different));
  gneiss::engine::resource_service isolated{isolated_owner};
  if (isolated.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  render_asset_ref conflict;
  check(isolated.reserve_asset(source_a, conflict) == gneiss::engine::result::success &&
        conflict != a && gneiss::engine::asset_rid(conflict) != gneiss::engine::asset_rid(a));
  check(!isolated.owns(a));
  auto& shared = loader;
  if (shared.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  check(shared.reserve_asset(source_a, conflict) == gneiss::engine::result::success &&
        gneiss::engine::asset_rid(conflict) == gneiss::engine::asset_rid(a));

  const auto stage = [&](const auto& source, const auto& target, auto sequence, auto& output) {
    decoded_asset prepared;
    prepared.uri = source.uri;
    prepared.content =
        asset_ref<const material_description>{std::make_shared<const material_description>()};
    return gneiss::engine::detail::asset_update::prepare_update(loader, std::move(prepared), output,
                                                                target, sequence);
  };
  std::array<detail::asset_update, 1> first, unrelated, late, latest;
  const auto first_sequence = gneiss::engine::detail::asset_update::next_update_sequence();
  check(stage(source_a, a, first_sequence, first.front()) == gneiss::engine::result::success);
  check(stage(source_b, b, gneiss::engine::detail::asset_update::next_update_sequence(),
              unrelated.front()) == gneiss::engine::result::success);
  check(gneiss::engine::detail::asset_update::apply_update(loader, unrelated.front()) ==
        gneiss::engine::result::success);
  check(gneiss::engine::detail::asset_update::apply_update(loader, first.front()) ==
        gneiss::engine::result::success);
  const auto previous = resources.share_material(gneiss::engine::asset_rid(a));
  const auto old_sequence = gneiss::engine::detail::asset_update::next_update_sequence();
  check(stage(source_a, a, old_sequence, late.front()) == gneiss::engine::result::success);
  const auto new_sequence = gneiss::engine::detail::asset_update::next_update_sequence();
  check(gneiss::engine::detail::asset_update::claim_update(loader, a, new_sequence));
  // 新请求尚未完成也阻止旧任务覆盖；最新请求失败时仍保留当前成功内容。
  check(gneiss::engine::detail::asset_update::apply_update(loader, late.front()) ==
        gneiss::engine::result::invalid_state);
  check(resources.share_material(gneiss::engine::asset_rid(a)) == previous);
  check(stage(source_a, a, old_sequence, late.front()) == gneiss::engine::result::invalid_state);
  check(stage(source_a, a, new_sequence, latest.front()) == gneiss::engine::result::success);
  check(gneiss::engine::detail::asset_update::apply_update(loader, latest.front()) ==
        gneiss::engine::result::success);
  // 内容互斥；空结果和类型不匹配不得占用序号或替换已有材质。
  const auto retained = resources.share_material(asset_rid(a));
  decoded_asset invalid;
  invalid.uri = source_a.uri;
  detail::asset_update rejected;
  check(detail::asset_update::prepare_update(loader, invalid, rejected, a) ==
        result::invalid_argument);
  invalid.content = asset_ref<const material_description>{};
  check(detail::asset_update::prepare_update(loader, invalid, rejected, a) ==
        result::invalid_argument);
  invalid.content = texture_data{};
  check(detail::asset_update::prepare_update(loader, invalid, rejected, a) ==
        result::invalid_argument);
  auto invalid_update = latest.front();
  invalid_update.data = std::monostate{};
  check(detail::asset_update::apply_update(loader, invalid_update) == result::invalid_argument);
  invalid_update.data = std::shared_ptr<const texture_data>{};
  check(detail::asset_update::apply_update(loader, invalid_update) == result::invalid_argument);
  invalid_update.data = std::make_shared<const texture_data>();
  check(detail::asset_update::apply_update(loader, invalid_update) == result::invalid_argument);
  invalid_update.data = detail::asset_update::material_content{};
  check(detail::asset_update::apply_update(loader, invalid_update) == result::invalid_argument);
  check(detail::asset_update::apply_update(loader, latest.front()).ok());
  check(resources.share_material(asset_rid(a)) == retained);
}
void reserved_material_lifetime() {
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::result::success);
  render_service resources;
  gneiss::engine::render_resource_factory resources_factory{resources};
  gneiss::engine::asset_manager asset_owner(gneiss::test_support::test_files(files));
  [[maybe_unused]] auto& cache = asset_owner.cache();
  gneiss::engine::resource_service loader{asset_owner};
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  const asset_source request{"asset://material", asset_type::material};
  render_asset_ref target;
  check(loader.reserve_asset(request, target) == gneiss::engine::result::success);
  const auto rid = gneiss::engine::asset_rid(target);
  decoded_asset prepared;
  prepared.uri = request.uri;
  prepared.content =
      asset_ref<const material_description>{std::make_shared<const material_description>()};
  std::array<detail::asset_update, 1> candidates;
  check(gneiss::engine::detail::asset_update::prepare_update(loader, std::move(prepared),
                                                             candidates.front(), target) ==
        gneiss::engine::result::success);
  check(resources.material_status(rid) == gneiss::engine::result::not_ready);
  check(gneiss::engine::detail::asset_update::apply_update(loader, candidates.front()) ==
        gneiss::engine::result::success);
  candidates = {};
  check(resources.material_status(rid) == gneiss::engine::result::success);
  target = {};
  loader.collect_unused();
  check(resources.material_status(rid) == gneiss::engine::result::invalid_handle);
}
void cpu_loading(execution_mode mode) {
  task_scheduler scheduler({.mode = mode});
  auto backend = std::make_shared<delayed_backend>();
  auto files =
      std::make_shared<gneiss::engine::files::service>(gneiss::engine::files::service_options{
          .max_file_bytes = resource_service::maximum_prepared_bytes});
  check(files->mount({.prefix = "asset://", .readers = {backend}, .writer = {}}).ok());
  render_service resources;
  gneiss::engine::render_resource_factory resources_factory{resources};
  asset_manager manager(gneiss::test_support::test_files(files));
  gneiss::engine::resource_service loader{manager};
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  check(loader.configure_loading(scheduler).ok());
  auto& service = loader;
  memory_files fixture;
  fixture.pixel(std::byte{42});
  fixture.text(
      "texture",
      R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})");
  const std::array<std::string, 1> uris{"asset://texture"};
  std::uint64_t request{};
  asset_load_completion completed;
  auto advance = [&] {
    (void)scheduler.run_ready();
    check(files->dispatch_completions(64).ok());
    manager.advance();
    loader.advance();
    std::this_thread::yield();
  };
  auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      advance();
    }
  };
  check(service.submit(uris, 1U, 1U, request).ok());
  auto target = service.requested_assets().front();
  until([&] { return bool(backend->callback); });
  check(resources.texture_status(asset_rid(target)) == result::not_ready);
  check(!service.take(completed));
  backend->complete(fixture.files.at("texture"));
  until([&] { return bool(backend->callback); });
  check(backend->path == "image.ktx2");
  backend->complete(fixture.files.at("image.ktx2"));
  until([&] { return service.take(completed); });
  // 没有 Render 或 GPU 回执，CPU 数据就绪即成功。
  check(completed.result.ok() && completed.state == asset_load_state::applied);
  check(resources.texture_status(asset_rid(target)).ok());
  const auto previous = resources.share_texture(asset_rid(target));
  check(previous && previous->levels.front().pixels.front() == std::byte{42});
  const auto reads = backend->reads;
  check(service.submit(uris, 1U, 2U, request, false).ok());
  until([&] { return service.take(completed); });
  check(completed.result.ok() && backend->reads == reads);
  check(service.submit(uris, 1U, 3U, request).ok());
  until([&] { return bool(backend->callback); });
  backend->complete({}, result::io);
  until([&] { return service.take(completed); });
  check(completed.result == result::io && resources.share_texture(asset_rid(target)) == previous);
  check(service.submit(uris, 1U, 4U, request).ok());
  until([&] { return bool(backend->callback); });
  check(service.cancel());
  until([&] { return service.take(completed); });
  check(completed.state == asset_load_state::cancelled);
  backend->complete(fixture.files.at("texture"));
  check(files->dispatch_completions(64U).ok());
  check(resources.share_texture(asset_rid(target)) == previous);
  fixture.text(
      "mesh",
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0.0,0.0,1.0],[0.0,0.0,1.0],[0.0,0.0,1.0]]})");
  fixture.text(
      "material",
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":"asset://texture"})");
  const std::array mixed{asset_source{"asset://mesh", asset_type::mesh},
                         asset_source{"asset://material", asset_type::material}};
  check(service.submit_assets(mixed, 1U, 5U, request).ok());
  auto mesh_target = service.requested_assets().front();
  auto material_target = service.requested_assets().back();
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    return service.take(completed);
  });
  auto material = asset_cast<material_resource>(material_target);
  check(completed.result.ok() && resources.mesh_status(asset_rid(mesh_target)).ok());
  check(material && material->textures()[0] == asset_cast<texture_resource>(target));
  // 已采用网格之后取消后续材质读取，不回滚成功项或既有依赖。
  check(service.submit_assets(mixed, 1U, 6U, request).ok());
  until([&] {
    if (backend->callback && backend->path == "mesh")
      backend->complete(fixture.files.at("mesh"));
    return backend->callback && backend->path == "material";
  });
  const auto adopted_mesh = resources.share_mesh(asset_rid(mesh_target));
  check(service.cancel());
  until([&] { return service.take(completed); });
  check(completed.state == asset_load_state::cancelled && completed.results.front().applied);
  backend->complete(fixture.files.at("material"));
  check(files->dispatch_completions(64U).ok());
  check(resources.share_mesh(asset_rid(mesh_target)) == adopted_mesh);
  // CPU 字节预算失败不破坏已采用内容。
  check(service.submit_assets(std::span(mixed).first(1U), 1U, 7U, request, true, 1U).ok());
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    return service.take(completed);
  });
  check(completed.result.failed() && resources.share_mesh(asset_rid(mesh_target)) == adopted_mesh);
  loader.request_stop();
  until([&] { return loader.stopped(); });
  check(!manager.stopped()); // 资源服务停止不关闭共享的 CPU 加载器。
  texture rejected;
  check(loader.reserve_texture(rejected) == result::invalid_state && !rejected);
  check(loader.configure_loading(scheduler) == result::invalid_state);
  check(service.submit(uris, 1U, 8U, request) == result::invalid_state && request == 0U);
}
} // namespace
int main() try {
  prepared_source_revisions();
  mesh_data_lifetime();
  independent_update_sequences();
  reserved_material_lifetime();
  cpu_loading(execution_mode::cooperative);
  cpu_loading(execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
