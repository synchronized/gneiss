// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/serialization/binding.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/input/action_map.hpp"
#include <cstring>

namespace {
std::span<const std::byte> bytes(std::string_view text) {
  return std::as_bytes(std::span{text.data(), text.size()});
}

void advance_assets_for_test(gneiss::engine::asset_manager& assets) {
  if (const auto files = assets.async_files())
    (void)files->dispatch_completions(64);
  assets.advance();
}
gneiss::engine::error load_map_for_test(gneiss::engine::asset_manager& assets,
                                        gneiss::engine::task_scheduler& executor,
                                        std::string_view uri,
                                        gneiss::engine::action_map_description& output) {
  gneiss::engine::asset_request<gneiss::engine::action_map> request;
  auto status = gneiss::test_support::assign_value(
      assets.reload<typename std::remove_cvref_t<decltype(request)>::value_type>(uri), request);
  if (status.failed())
    return status;
  gneiss::engine::asset_ref<const gneiss::engine::action_map> value;
  for (unsigned i = 0; i < 32; ++i) {
    advance_assets_for_test(assets);
    (void)executor.run_ready();
    status = gneiss::test_support::take_request(request, value);
    if (status != gneiss::engine::error::not_ready) {
      if (status.ok())
        output = *value->snapshot();
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
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  gneiss::engine::action_map_description map;
  constexpr auto valid =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":-1.0},{"key":7,"scale":1.0}]}]})";
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<gneiss::engine::action_map_description>(
              bytes(valid)),
          map) != gneiss::engine::error::success ||
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
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<gneiss::engine::action_map_description>(
              bytes(duplicate)),
          map) != gneiss::engine::error::invalid_argument ||
      gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<gneiss::engine::action_map_description>(
              bytes(unknown_key)),
          map) != gneiss::engine::error::invalid_argument ||
      gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<gneiss::engine::action_map_description>(
              bytes(invalid_scale)),
          map) != gneiss::engine::error::invalid_argument ||
      map.actions[0].bindings.size() != original_size) {
    return 2;
  }
  auto backend = std::make_shared<changing_files>();
  gneiss::test_support::fixture_files files;
  if (files.mount("asset://", backend) != gneiss::engine::error::success)
    return 3;
  auto& executor = gneiss::test_support::asset_tasks();
  gneiss::engine::asset_manager assets(gneiss::test_support::test_files(files));
  assets.configure(executor);
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
  // 编码、解码与资源加载共用默认表；字段保持 input-map v1 布局。
  auto codec = serialization::find_serialization(runtime_type_of<action_map_description>());
  if (!codec || !codec->reflected || codec->decode || codec->encode)
    return 9;
  serialization::diagnostic diagnostic;
  for (auto version : {2, 3}) {
    const auto text = std::string("{\"format\":\"gneiss.input-map\",\"version\":") +
                      std::to_string(version) + ",\"actions\":[]}";
    auto unsupported = gneiss::engine::asset_file::deserialize<action_map_description>(text);
    if (unsupported || unsupported.error().error != error::unsupported ||
        unsupported.error().path != "/version")
      return 28;
  }
  auto saved_bytes = gneiss::engine::asset_file::serialize_bytes(map);
  if (!saved_bytes)
    return 10;
  const asset_file::format exact{"gneiss.input-map", 1, saved_bytes->size()};
  auto short_limit = exact;
  --short_limit.maximum_bytes;
  serialization::context ctx;
  auto view = meta::view(std::as_const(map));
  if (!view || !asset_file::serialize_instance(ctx, exact, *view) ||
      !asset_file::deserialize_object(ctx, exact, runtime_type_of<action_map_description>(),
                                      *saved_bytes) ||
      asset_file::serialize_instance(ctx, short_limit, *view) ||
      asset_file::deserialize_object(ctx, short_limit, runtime_type_of<action_map_description>(),
                                     *saved_bytes))
    return 31;
  auto roundtrip = gneiss::engine::asset_file::deserialize<action_map_description>(*saved_bytes);
  if (!roundtrip || roundtrip->actions.front().name != "move" ||
      roundtrip->actions.front().bindings.front().physical_key != 4 ||
      roundtrip->actions.front().bindings.front().scale != -1)
    return 20;
  for (const auto malformed :
       {R"({"format":"gneiss.input-map","format":"gneiss.input-map","version":1,"actions":[]})",
        R"({"format":"gneiss.input-map","version":1,"version":1,"actions":[]})",
        R"({"format":"wrong","version":1,"actions":[]})",
        R"({"format":"gneiss.input-map","version":"1","actions":[]})"}) {
    auto result = gneiss::engine::asset_file::deserialize<gneiss::engine::action_map_description>(
        bytes(malformed));
    if (result || result.error().error != error::invalid_argument)
      return 30;
  }
  auto bad_map = map;
  bad_map.actions.push_back(bad_map.actions.front());
  if (gneiss::engine::asset_file::serialize_bytes(bad_map))
    return 21;
  bad_map = map;
  bad_map.actions.front().bindings.front().scale = 0;
  if (gneiss::engine::asset_file::serialize_bytes(bad_map))
    return 22;
  bad_map = map;
  bad_map.actions.front().name.assign(maximum_action_map_bytes, 'x');
  if (gneiss::engine::asset_file::serialize_bytes(bad_map))
    return 23;
  for (const std::string_view body :
       {R"({"name":"move","bindings":[{"key":4294967296,"scale":1}]})",
        R"({"name":"move","bindings":[{"key":4,"key":7,"scale":1}]})",
        R"({"name":"move","bindings":[{"key":4,"scale":1,"unknown":1}]})",
        R"({"name":"move","bindings":[]})", R"({"name":"move"})"}) {
    const std::string malformed =
        std::string(R"({"format":"gneiss.input-map","version":1,"actions":[)") + std::string(body) +
        "]}";
    if (gneiss::engine::asset_file::deserialize<gneiss::engine::action_map_description>(
            bytes(malformed)))
      return 24;
  }
  const auto invalid = std::as_bytes(std::span{invalid_scale, std::strlen(invalid_scale)});
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(map)>>(invalid), map,
          diagnostic) != error::invalid_argument ||
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
  auto old_snapshot = shared->snapshot();
  backend->first = backend->second = valid;
  auto changed_text = std::string(valid);
  const auto name_position = changed_text.find("move");
  changed_text.replace(name_position, 4, "jump");
  backend->first = backend->second = changed_text;
  if (gneiss::test_support::load_asset(assets, "asset://input/shared", shared, true).failed() ||
      shared != previous || shared->snapshot()->actions.front().name != "jump" ||
      old_snapshot->actions.front().name != "move" ||
      shared->source().uri != "asset://input/shared" ||
      shared->type() != runtime_type_of<action_map>())
    return 26;
  if (assets.load<action_map_description>("asset://input/shared").error() != error::unsupported ||
      assets.find<action_map_description>("asset://input/shared").error() != error::unsupported ||
      assets.store<action_map_description>("asset://input/shared", old_snapshot.shared()).error() !=
          error::unsupported)
    return 27;
  backend->first = backend->second = valid;
  asset_manager asynchronous(gneiss::test_support::test_files(files));
  asynchronous.configure(executor);
  asset_request<action_map> request;
  if (gneiss::test_support::assign_value(asynchronous.reload<action_map>("asset://input/map"),
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
  if (status.failed() || shared->snapshot()->actions.front().name != "move")
    return 16;
  previous = shared;
  backend->first = backend->second = invalid_scale;
  if (gneiss::test_support::assign_value(asynchronous.reload<action_map>("asset://input/map"),
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
  if (gneiss::test_support::assign_value(asynchronous.reload<action_map>("asset://input/oversized"),
                                         request)
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
  custom.configure(executor);
  const auto override_decode =
      +[](std::span<const std::byte>) -> result<action_map_description, serialization::diagnostic> {
    action_map_description output;
    output.actions = {{.name = "override", .bindings = {{.physical_key = 4, .scale = 1.0F}}}};
    return output;
  };
  if (custom.register_decoder<action_map_description>(override_decode).failed() ||
      load_map_for_test(custom, executor, "asset://input/map", map).failed() ||
      map.actions.front().name != "override")
    return 19;
  return 0;
}
