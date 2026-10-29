// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/input/action_map.hpp"
#include "engine/resource/input/action_map_codec.hpp"
#include <cstring>

namespace {
void advance_assets_for_test(gneiss::engine::asset_manager& assets) {
  if (const auto files = assets.async_files())
    (void)files->dispatch_completions(64);
  assets.advance();
}
gneiss::engine::error load_map_for_test(gneiss::engine::asset_manager& assets,
                                        gneiss::engine::task_scheduler& executor,
                                        std::string_view uri, gneiss::engine::action_map& output) {
  gneiss::engine::asset_request<gneiss::engine::action_map> request;
  auto status = gneiss::test_support::assign_value(
      assets.load<typename std::remove_cvref_t<decltype(request)>::value_type>(uri, true), request);
  if (status.failed())
    return status;
  gneiss::engine::asset_ref<const gneiss::engine::action_map> value;
  for (unsigned i = 0; i < 32; ++i) {
    advance_assets_for_test(assets);
    (void)executor.run_ready();
    status = gneiss::test_support::take_request(request, value);
    if (status != gneiss::engine::error::not_ready) {
      if (status.ok())
        output = *value;
      return status;
    }
  }
  return gneiss::engine::error::internal;
}
class changing_files final : public gneiss::test_support::memory_file_fixture {
public:
  std::string first, second;
  mutable unsigned reads = 0;
  mutable std::size_t last_limit{};
  bool fail_verify = false;
  gneiss::engine::error read(std::string_view, std::vector<std::byte>&) const noexcept override {
    return gneiss::engine::error::internal; // 测试禁止回退到无界读取。
  }
  gneiss::engine::error read_bounded(std::string_view, std::size_t limit,
                                     std::vector<std::byte>& output) const noexcept override {
    last_limit = limit;
    const auto& text = reads++ == 0 ? first : second;
    if (fail_verify && reads > 1)
      return gneiss::engine::error::io;
    if (text.size() > limit)
      return gneiss::engine::error::invalid_argument;
    try {
      output.resize(text.size());
    } catch (...) {
      return gneiss::engine::error::out_of_memory;
    }
    std::memcpy(output.data(), text.data(), text.size());
    return gneiss::engine::error::success;
  }
};
}

int main() {
  gneiss::engine::action_map map;
  constexpr auto valid =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":-1.0},{"key":7,"scale":1.0}]}]})";
  if (gneiss::test_support::assign_decoded(gneiss::engine::parse_action_map(valid), map) !=
          gneiss::engine::error::success ||
      map.actions.size() != 1U || map.actions[0].bindings.size() != 2U) {
    return 1;
  }
  const auto original_size = map.actions[0].bindings.size();
  constexpr auto duplicate =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":1}]},{"name":"move","bindings":[{"key":7,"scale":1}]}]})";
  constexpr auto unknown_key =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":256,"scale":1}]}]})";
  constexpr auto invalid_scale =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":0}]}]})";
  if (gneiss::test_support::assign_decoded(gneiss::engine::parse_action_map(duplicate), map) !=
          gneiss::engine::error::invalid_argument ||
      gneiss::test_support::assign_decoded(gneiss::engine::parse_action_map(unknown_key), map) !=
          gneiss::engine::error::invalid_argument ||
      gneiss::test_support::assign_decoded(gneiss::engine::parse_action_map(invalid_scale), map) !=
          gneiss::engine::error::invalid_argument ||
      map.actions[0].bindings.size() != original_size) {
    return 2;
  }
  auto backend = std::make_shared<changing_files>();
  gneiss::test_support::fixture_files files;
  if (files.mount("asset://", backend) != gneiss::engine::error::success)
    return 3;
  auto& executor = gneiss::test_support::asset_tasks();
  gneiss::engine::asset_manager assets(gneiss::test_support::test_files(files));
  assets.attach_task_executor(executor);
  backend->first = valid;
  backend->second = valid;
  if (load_map_for_test(assets, executor, "asset://input/map", map) !=
          gneiss::engine::error::success ||
      backend->reads != 1)
    return 4;
  // 本次读取完成后来源变化或后续读取失败，不触发二次读取。
  backend->reads = 0;
  backend->second.replace(backend->second.find("move"), 4, "turn");
  if (load_map_for_test(assets, executor, "asset://input/map", map) !=
          gneiss::engine::error::success ||
      backend->reads != 1 || map.actions[0].name != "move")
    return 5;
  backend->reads = 0;
  backend->fail_verify = true;
  if (load_map_for_test(assets, executor, "asset://input/map", map) !=
          gneiss::engine::error::success ||
      backend->reads != 1 || map.actions[0].name != "move")
    return 6;
  backend->reads = 0;
  backend->first.assign(gneiss::engine::maximum_action_map_bytes + 1, ' ');
  if (load_map_for_test(assets, executor, "asset://input/map", map) !=
          gneiss::engine::error::invalid_argument ||
      backend->reads != 1 || map.actions[0].name != "move")
    return 7;
  using namespace gneiss::engine;
  // 异步打开来源后检查读取上限，不调用旧 read_bounded。
  gneiss::engine::meta::registry_ref registry;
  gneiss::engine::meta::serialization_binding codec;
  if (gneiss::test_support::assign_value(builtin_resource_registry(), registry).failed() ||
      gneiss::test_support::assign_value(
          find_serialization(registry, runtime_type_of<action_map>()), codec)
          .failed() ||
      !codec.decode || codec.encode)
    return 9;
  gneiss::engine::meta::serialization_diagnostic diagnostic;
  std::vector<std::byte> encoded{std::byte{42}};
  if (gneiss::test_support::assign_decoded(serialize(registry, map), encoded, diagnostic) !=
          error::unsupported ||
      encoded != std::vector{std::byte{42}})
    return 10;
  const auto invalid = std::as_bytes(std::span{invalid_scale, std::strlen(invalid_scale)});
  if (gneiss::test_support::assign_decoded(
          deserialize<std::remove_cvref_t<decltype(map)>>(registry, invalid), map, diagnostic) !=
          error::invalid_argument ||
      diagnostic.path != "/actions/0/bindings/0" || diagnostic.message.empty() ||
      map.actions.front().name != "move")
    return 11;
  backend->reads = 0;
  backend->fail_verify = false;
  backend->first = backend->second = valid;
  asset_ref<const action_map> shared;
  if (gneiss::test_support::load_asset(assets, "asset://input/shared", shared).failed())
    return 12;
  auto previous = shared;
  if (gneiss::test_support::load_asset(assets, "asset://input/shared", shared).failed() ||
      shared != previous || backend->reads != 1)
    return 13;
  asset_manager asynchronous(gneiss::test_support::test_files(files));
  asynchronous.attach_task_executor(executor);
  asset_request<action_map> request;
  if (gneiss::test_support::assign_value(asynchronous.load<action_map>("asset://input/map", true),
                                         request)
          .failed())
    return 14;
  auto status = error::not_ready;
  for (unsigned i = 0; i != 32 && status == error::not_ready; ++i) {
    if (executor.run_ready().status != drive_status::success)
      return 15;
    advance_assets_for_test(asynchronous);
    status = gneiss::test_support::take_request(request, shared, &diagnostic);
  }
  if (status.failed() || shared->actions.front().name != "move")
    return 16;
  previous = shared;
  backend->first = backend->second = invalid_scale;
  if (gneiss::test_support::assign_value(asynchronous.load<action_map>("asset://input/map", true),
                                         request)
          .failed())
    return 17;
  status = error::not_ready;
  for (unsigned i = 0; i != 32 && status == error::not_ready; ++i) {
    (void)executor.run_ready();
    advance_assets_for_test(asynchronous);
    status = gneiss::test_support::take_request(request, shared, &diagnostic);
  }
  if (status != error::invalid_argument || shared != previous ||
      diagnostic.path != "/actions/0/bindings/0")
    return 18;
  // 异步读取在打开来源后检查字节上限，不再依赖旧 read_bounded 的参数传递。
  backend->first = backend->second = std::string(maximum_action_map_bytes + 1, ' ');
  if (gneiss::test_support::assign_value(
          asynchronous.load<action_map>("asset://input/oversized", true), request)
          .failed())
    return 24;
  status = error::not_ready;
  for (unsigned i = 0; i < 32 && status == error::not_ready; ++i) {
    (void)executor.run_ready();
    advance_assets_for_test(asynchronous);
    status = gneiss::test_support::take_request(request, shared);
  }
  if (status != error::invalid_argument || shared != previous)
    return 25;
  backend->first = backend->second = invalid_scale;
  asset_manager custom(gneiss::test_support::test_files(files));
  custom.attach_task_executor(executor);
  const auto override_decode =
      +[](std::span<const std::byte>) -> result<action_map, meta::serialization_diagnostic> {
    action_map output;
    output.actions = {{.name = "override", .bindings = {{.physical_key = 4, .scale = 1.0F}}}};
    return output;
  };
  if (custom.register_decoder<action_map>(override_decode, maximum_action_map_bytes).failed() ||
      load_map_for_test(custom, executor, "asset://input/map", map).failed() ||
      map.actions.front().name != "override")
    return 19;
  return 0;
}
