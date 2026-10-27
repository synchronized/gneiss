// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_asset.hpp"
#include "../../support/memory_file_fixture.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/core/type_id.hpp"
#include "engine/platform/files/file_backend.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_manager.hpp"
#include "engine/resource/material/material_codec.hpp"
#include "engine/resource/material/material_description.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace gneiss::test_support {
struct asset_value {
  static constexpr auto type_guid = gneiss::engine::material_description::type_guid;
  static constexpr std::string_view type_name = "test_asset";
  char byte{};
};
}
namespace {
void advance_assets_for_test(gneiss::engine::asset_manager& assets) {
  if (const auto files = assets.async_files())
    (void)files->dispatch_completions(64);
  assets.advance_resources();
}
using namespace gneiss;
using namespace gneiss::engine;
using namespace gneiss::engine;
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
using value = gneiss::test_support::asset_value;
class memory_files final : public gneiss::test_support::memory_file_fixture {
public:
  mutable unsigned reads{};
  mutable char byte{'a'};
  mutable bool change{};
  bool missing{}, oversized{};
  std::vector<std::byte> contents;
  gneiss::engine::result read(std::string_view path,
                              std::vector<std::byte>& output) const noexcept override {
    return read_bounded(path, 8, output);
  }
  gneiss::engine::result read_bounded(std::string_view, std::size_t limit,
                                      std::vector<std::byte>& output) const noexcept override {
    ++reads;
    if (missing)
      return gneiss::engine::result::not_found;
    if (oversized || limit < 1 || contents.size() > limit)
      return gneiss::engine::result::invalid_argument;
    try {
      output = contents.empty() ? std::vector{static_cast<std::byte>(byte)} : contents;
      if (change)
        ++byte;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
};
class loader final : public asset_decoder {
public:
  mutable unsigned decodes{};
  gneiss::engine::type_id target_type() const noexcept override {
    return gneiss::engine::runtime_type_of<value>();
  }
  std::size_t maximum_bytes() const noexcept override { return 8; }
  gneiss::engine::result decode(const std::vector<std::byte>& bytes,
                                std::shared_ptr<const void>& output,
                                gneiss::engine::meta::serialization_diagnostic&) const override {
    ++decodes;
    if (bytes.empty() || bytes[0] == std::byte{'!'})
      return gneiss::engine::result::invalid_argument;
    output = std::make_shared<const value>(value{static_cast<char>(bytes[0])});
    return gneiss::engine::result::success;
  }
};
result explicit_decode(std::span<const std::byte> bytes, value& output,
                       gneiss::engine::meta::serialization_diagnostic&) {
  output.byte = 'x';
  return bytes.front() == std::byte{'!'} ? result::invalid_argument : result::success;
}
class delayed_files final : public gneiss::engine::files::backend {
public:
  gneiss::engine::files::callback<gneiss::engine::files::bytes> pending;
  result read(std::string_view, gneiss::engine::files::read_options,
              gneiss::engine::files::callback<gneiss::engine::files::bytes> callback) override {
    pending = std::move(callback);
    return result::success;
  }
  void finish(char value) {
    auto callback = std::move(pending);
    callback({result::success, {static_cast<std::byte>(value)}});
  }
};
void injected_file_service() {
  auto backend = std::make_shared<delayed_files>();
  auto files = std::make_shared<gneiss::engine::files::service>(
      gneiss::engine::files::service_options{1, 64});
  check(files->mount({"asset://", {backend}, {}}).ok(), "service mount");
  auto& executor = gneiss::test_support::asset_tasks();
  auto assets = std::make_unique<asset_manager>(gneiss::test_support::test_files(files));
  auto decoder = std::make_shared<loader>();
  check(assets->register_decoder(decoder).ok(), "service decoder");
  assets->attach_task_executor(executor);
  asset_request<value> first, second;
  asset_ref<const value> loaded, other;
  check(assets->find_loaded("asset://missing", loaded) == result::not_found,
        "cache miss does not trigger IO");
  check(assets->load("asset://value", first).ok(), "service request");
  check(assets->load("asset://value", second).ok(), "service coalescing");
  asset_request<value> full;
  check(assets->load("asset://other", full) == result::not_ready && !full,
        "file capacity propagates");
  for (unsigned i = 0; i < 8; ++i)
    advance_assets_for_test(*assets);
  check(executor.stats().submitted == 0 && decoder->decodes == 0,
        "IO waiting submits no CPU polling task");
  backend->finish('q');
  advance_assets_for_test(*assets);
  check(executor.stats().submitted == 1 && decoder->decodes == 0,
        "one decode after file completion");
  check(executor.run_ready().status == drive_status::success, "decode task");
  advance_assets_for_test(*assets);
  check(first.take(loaded).ok() && second.take(other).ok() && loaded == other &&
            loaded->byte == 'q',
        "shared decoded result");
  check(assets->find_loaded("asset://value", other).ok() && other == loaded,
        "service cache readable synchronously");
  check(assets->find_loaded("asset://missing", other) == result::not_found && other == loaded,
        "cache miss preserves output");
  std::shared_ptr<const gneiss::engine::files::source_domain> source;
  check(files->resolve_source("asset://value", source).ok() && source &&
            assets->cache().size() == 1,
        "service cache source identity");
  check(assets->load("asset://value", first, true).ok(), "cancel service load");
  first.cancel();
  advance_assets_for_test(*assets);
  backend->finish('r');
  advance_assets_for_test(*assets);
  check(executor.stats().submitted == 1, "cancelled IO never decodes");
  check(assets->find_loaded("asset://value", other).ok() && other == loaded,
        "cancel preserves cache");
  check(assets->load("asset://late", first).ok(), "late request");
  assets->request_stop_resources();
  check(!assets->resources_stopped(), "stop completion requires owner advance");
  assets->advance_resources();
  check(assets->resources_stopped(), "manager stops without waiting for file callback");
  assets.reset();
  check(first.take(other) == result::invalid_state && other == loaded, "stop terminates observer");
  backend->finish('s');
  check(files->dispatch_completions(64).ok() && executor.stats().submitted == 1,
        "late IO cannot access destroyed manager");
  bool closed = false;
  check(files->shutdown([&](result status) { closed = status.ok(); }).ok(),
        "shared service shutdown");
  check(files->dispatch_completions(64).ok() && closed, "shared service drained");
}
void decoder_selection() {
  auto& executor = gneiss::test_support::asset_tasks();
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == result::success, "decoder mount");
  gneiss::engine::meta::registry_ref registry;
  check(create_registry(registry) == result::success, "decoder registry");
  gneiss::engine::meta::serialization_binding codec;
  codec.decode = [](std::span<const std::byte> bytes, void* object,
                    gneiss::engine::meta::serialization_diagnostic& diagnostic) {
    if (bytes.front() == std::byte{'?'})
      diagnostic = {7U, "/value", "invalid value"};
    static_cast<value*>(object)->byte = 'r';
    return bytes.front() == std::byte{'?'} ? result::invalid_argument : result::success;
  };
  check(register_type<value>(registry, {.guid = material_description::type_guid,
                                        .schema_version = 1U,
                                        .name = "test_asset",
                                        .serialization = codec}) == result::success,
        "reflection decoder registration");
  check(freeze(registry) == result::success, "freeze decoder registry");
  asset_manager reflected(gneiss::test_support::test_files(files), registry);
  registry.reset();
  asset_ref<const value> output;
  check(gneiss::test_support::load_asset(reflected, "asset://value", output, true) ==
                result::success &&
            output->byte == 'r',
        "reflection fallback retains registry");
  const auto cached_reads = source->reads;
  auto cached_output = output;
  check(gneiss::test_support::load_asset(reflected, "asset://value", output) == result::success &&
            output == cached_output && source->reads == cached_reads,
        "load reuses cache");
  auto previous = output;
  source->byte = '?';
  gneiss::engine::meta::serialization_diagnostic failure;
  check(gneiss::test_support::load_asset(reflected, "asset://value", output, true, &failure) ==
                result::invalid_argument &&
            output == previous,
        "reflection failure preserves output");
  check(failure.byte_offset == 7U && failure.path == "/value" && failure.message == "invalid value",
        "load diagnostic");
  check(reflected.register_decoder<value>(&explicit_decode, 8) == result::invalid_state,
        "fallback freezes registration");
  source->byte = 'a';
  reflected.attach_task_executor(executor);
  task_scheduler other_executor({.workers = 0, .mode = execution_mode::cooperative});
  bool rejected_executor{};
  try {
    reflected.attach_task_executor(other_executor);
  } catch (const std::invalid_argument&) {
    rejected_executor = true;
  }
  check(rejected_executor, "manager rejects executor replacement");
  asset_request<value> request;
  check(reflected.load("asset://async", request) == result::success, "async reflection request");
  auto async_status = request.take(output);
  for (unsigned i = 0; i != 32 && async_status == result::not_ready; ++i) {
    check(executor.run_ready().status == drive_status::success, "reflection drive");
    advance_assets_for_test(reflected);
    async_status = request.take(output);
  }
  check(async_status == result::success && output->byte == 'r', "async reflection decode");
  source->byte = '?';
  previous = output;
  check(reflected.load("asset://async", request, true) == result::success, "async failure request");
  async_status = result::not_ready;
  for (unsigned i = 0; i != 32 && async_status == result::not_ready; ++i) {
    check(executor.run_ready().status == drive_status::success, "failure drive");
    advance_assets_for_test(reflected);
    async_status = request.take(output, &failure);
  }
  check(async_status == result::invalid_argument && output == previous &&
            failure.byte_offset == 7U && failure.path == "/value" &&
            failure.message == "invalid value",
        "async diagnostic and unchanged output");
  source->byte = 'a';
  // 同一个类型同时具备反射与显式解码器时，只采用显式函数。
  check(create_registry(registry) == result::success, "override registry");
  check(register_type<value>(registry, {.guid = material_description::type_guid,
                                        .schema_version = 1U,
                                        .name = "test_asset",
                                        .serialization = codec}) == result::success,
        "override reflection");
  check(freeze(registry) == result::success, "override freeze");
  asset_manager explicit_assets(gneiss::test_support::test_files(files), registry);
  check(explicit_assets.register_decoder<value>(&explicit_decode, 8) == result::success,
        "typed decoder");
  check(explicit_assets.register_decoder<value>(&explicit_decode, 8) == result::invalid_argument,
        "duplicate decoder");
  check(gneiss::test_support::load_asset(explicit_assets, "asset://explicit", output, true) ==
                result::success &&
            output->byte == 'x',
        "explicit decoder wins");
  previous = output;
  source->byte = '!';
  check(gneiss::test_support::load_asset(explicit_assets, "asset://explicit", output, true) ==
                result::invalid_argument &&
            output == previous,
        "explicit failure does not fall back");
  asset_manager unknown(gneiss::test_support::test_files(files));
  const auto reads = source->reads;
  check(gneiss::test_support::load_asset(unknown, "asset://unknown", output) ==
                result::unsupported &&
            source->reads == reads,
        "missing decoder fails before IO");
  asset_manager native(gneiss::test_support::test_files(files));
  check(native.register_decoder<value>(&explicit_decode, 8) == result::success,
        "unreflected decoder registration");
  source->byte = 'a';
  check(gneiss::test_support::load_asset(native, "asset://native", output) == result::success &&
            output->byte == 'x',
        "unreflected type loads");
  material_description description;
  description.parameters.metallic = 0.25F;
  gneiss::engine::meta::serialization_diagnostic diagnostic;
  check(encode_material(description, source->contents, diagnostic) == result::success,
        "material fixture");
  asset_manager material_assets(gneiss::test_support::test_files(files));
  asset_ref<const material_description> material;
  check(gneiss::test_support::load_asset(material_assets, "asset://material", material) ==
                result::success &&
            material->parameters.metallic == 0.25F,
        "builtin material reflection load");
  asset_manager limited(gneiss::test_support::test_files(files), {}, 1);
  auto saved = material;
  check(gneiss::test_support::load_asset(limited, "asset://material", material) ==
                result::invalid_argument &&
            material == saved,
        "reflection input limit preserves output");
  reflected.stop_resources();
}
void reference_lifetime() {
  struct tracked {
    explicit tracked(unsigned& count) : destroyed(count) {}
    ~tracked() { ++destroyed; }
    unsigned& destroyed;
    int payload{7};
  };
  static_assert(!std::constructible_from<asset_ref<tracked>, tracked*>);
  static_assert(!std::constructible_from<asset_ref<tracked>, asset_ref<const tracked>>);
  unsigned destroyed = 0;
  asset_ref<tracked> owner{std::make_shared<tracked>(destroyed)};
  asset_ref<const tracked> read_only = owner;
  auto field = owner.alias(&owner->payload);
  auto moved = std::move(owner);
  check(!owner && moved && read_only->payload == 7, "move and const conversion");
  moved.reset();
  read_only.reset();
  check(destroyed == 0 && *field == 7, "alias retains owner");
  field.reset();
  check(destroyed == 1, "last reference destroys exactly once");
  field.reset();
  check(destroyed == 1, "empty reset does not destroy twice");
}
void cache_collection() {
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::result::success, "collection mount");
  auto& executor = gneiss::test_support::asset_tasks();
  asset_manager assets(gneiss::test_support::test_files(files));
  check(assets.register_decoder(std::make_shared<loader>()) == gneiss::engine::result::success,
        "collection loader");
  assets.attach_task_executor(executor);
  asset_ref<const value> held;
  check(gneiss::test_support::load_asset(assets, "asset://value", held) ==
            gneiss::engine::result::success,
        "seed cache");
  check(assets.cache().size() == 1U, "CPU result uses unified index");
  assets.collect_unused();
  asset_request<value> request;
  check(assets.load("asset://value", request) == gneiss::engine::result::success, "cached load");
  asset_ref<const value> copy;
  check(request.take(copy) == gneiss::engine::result::success && held == copy && source->reads == 1,
        "external reference prevents collection");
  held.reset();
  copy.reset();
  assets.collect_unused();
  check(assets.cache().size() == 0U, "CPU entry collected after last reference");
  source->byte = 'b';
  check(assets.load("asset://value", request) == gneiss::engine::result::success,
        "load after collection");
  // 取消观察者后，活动任务仍必须拥有完成所需的索引。
  request.cancel();
  assets.collect_unused();
  check(executor.run_ready().status == drive_status::success, "cancelled work completes");
  advance_assets_for_test(assets);
  assets.collect_unused();
  check(assets.load("asset://value", request) == gneiss::engine::result::success,
        "retry collected load");
  for (unsigned i = 0; i < 10 && !copy; ++i) {
    check(executor.run_ready().status == drive_status::success, "collection drive");
    advance_assets_for_test(assets);
    assets.collect_unused();
    const auto status = request.take(copy);
    check(status == gneiss::engine::result::success || status == gneiss::engine::result::not_ready,
          "collection preserves active request");
  }
  check(copy && copy->byte == 'b', "new content after collection");
  source->byte = '!';
  check(assets.load("asset://value", request, true) == gneiss::engine::result::success,
        "reload before collection");
  copy.reset();
  assets.collect_unused();
  check(assets.cache().size() == 1U, "in-flight reload retains previous cache entry");
  auto reload_status = gneiss::engine::result::not_ready;
  for (unsigned i = 0; i < 10 && reload_status == gneiss::engine::result::not_ready; ++i) {
    check(executor.run_ready().status == drive_status::success, "failed reload drive");
    advance_assets_for_test(assets);
    reload_status = request.take(copy);
  }
  check(reload_status == gneiss::engine::result::invalid_argument,
        "reload fails without replacement");
  request.cancel();
  check(assets.load("asset://value", request) == gneiss::engine::result::success &&
            request.take(copy) == gneiss::engine::result::success && copy->byte == 'b',
        "failed reload retains cached content through collection");
  assets.stop_resources();
  check(copy->byte == 'b', "reference survives manager stop");
}
void run() {
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::result::success, "mount");
  auto& executor = gneiss::test_support::asset_tasks();
  auto parser = std::make_shared<loader>();
  asset_manager assets(gneiss::test_support::test_files(files));
  check(assets.register_decoder(parser) == gneiss::engine::result::success, "register");
  check(assets.register_decoder(parser) == gneiss::engine::result::invalid_argument,
        "duplicate registration");
  asset_request<value> first, second;
  check(assets.load("asset://value", first) == gneiss::engine::result::not_ready,
        "executor required");
  assets.attach_task_executor(executor);
  auto drive = [&] {
    check(executor.run_ready().status == drive_status::success, "drive");
    advance_assets_for_test(assets);
  };
  auto wait = [&](asset_request<value>& request, asset_ref<const value>& output) {
    for (unsigned i = 0; i < 100; ++i) {
      const auto status = request.take(output);
      if (status != gneiss::engine::result::not_ready)
        return status;
      drive();
    }
    throw std::runtime_error("request never settled");
  };
  check(assets.load("asset://value", first) == gneiss::engine::result::success, "first");
  check(source->reads == 0, "request does not perform inline I/O");
  check(assets.load("asset://value", second) == gneiss::engine::result::success, "second");
  asset_ref<const value> a, b;
  check(first.take(a) == gneiss::engine::result::not_ready, "initial pending");
  check(wait(first, a) == gneiss::engine::result::success &&
            wait(second, b) == gneiss::engine::result::success,
        "coalesced load");
  check(a == b && a->byte == 'a' && parser->decodes == 1 && source->reads == 1,
        "one read and read and decode shared");
  check(first.take(b) == gneiss::engine::result::invalid_state && b == a,
        "consume once preserves output");
  check(assets.register_decoder(std::make_shared<loader>()) ==
            gneiss::engine::result::invalid_state,
        "frozen");
  source->byte = 'b';
  check(assets.load("asset://value", first) == gneiss::engine::result::success, "cached");
  check(wait(first, b) == gneiss::engine::result::success && b == a,
        "cache does not reload implicitly");
  check(assets.load("asset://value", first, true) == gneiss::engine::result::success, "reload");
  check(assets.load("asset://value", second, true) == gneiss::engine::result::success,
        "reload subscriber");
  first.cancel();
  check(wait(second, b) == gneiss::engine::result::success && b->byte == 'b' && a->byte == 'a',
        "cancel one keeps independent subscriber and old value");

  // 旧请求解码完但尚未分发；显式采用新数据后，旧结果不能回写缓存。
  source->byte = 'c';
  check(assets.load("asset://value", first, true) == gneiss::engine::result::success, "old async");
  advance_assets_for_test(assets); // 文件快照先交给异步来源，解码尚未发布。
  check(executor.run_ready().status == drive_status::success, "old work ready");
  source->byte = 'd';
  check(assets.store<value>("asset://value", std::make_shared<const value>(value{'d'}), b) ==
                gneiss::engine::result::success &&
            b->byte == 'd',
        "store wins");
  check(wait(first, a) == gneiss::engine::result::success && a->byte == 'c',
        "old subscriber receives old result");
  check(assets.load("asset://value", first) == gneiss::engine::result::success,
        "cached after old completion");
  check(wait(first, a) == gneiss::engine::result::success && a == b,
        "old async cannot replace stored cache");

  source->byte = 'x';
  check(assets.load("asset://new", first) == gneiss::engine::result::success, "first-load overlap");
  advance_assets_for_test(assets); // 文件快照先交给异步来源，解码尚未发布。
  check(executor.run_ready().status == drive_status::success, "first-load work ready");
  source->byte = 'y';
  asset_ref<const value> independent;
  check(assets.store<value>("asset://new", std::make_shared<const value>(value{'y'}),
                            independent) == gneiss::engine::result::success,
        "first-load store");
  asset_ref<const value> old;
  check(wait(first, old) == gneiss::engine::result::success && old->byte == 'x',
        "first-load old observer");
  check(assets.load("asset://new", first) == gneiss::engine::result::success, "first-load current");
  check(wait(first, old) == gneiss::engine::result::success && old == independent,
        "first-load order");
  source->byte = 'z';
  check(assets.load("asset://new", first, true) == gneiss::engine::result::success,
        "async before failed store");
  advance_assets_for_test(assets); // 文件快照先交给异步来源，解码尚未发布。
  check(executor.run_ready().status == drive_status::success, "reload work ready");
  source->byte = '!';
  check(assets.store<value>("asset://new", {}, independent) ==
                gneiss::engine::result::invalid_argument &&
            independent->byte == 'y',
        "failed store preserves reference");
  check(wait(first, old) == gneiss::engine::result::success && old->byte == 'z',
        "failed store retains async rights");
  check(assets.load("asset://new", first) == gneiss::engine::result::success,
        "cache after failed store");
  check(wait(first, independent) == gneiss::engine::result::success && independent == old,
        "async published after failed store");

  source->byte = '!';
  check(gneiss::test_support::load_asset(assets, "asset://value", a, true) ==
                gneiss::engine::result::invalid_argument &&
            a == b,
        "load failure preserves output");
  check(assets.load("asset://value", first, true) == gneiss::engine::result::success,
        "invalid decode");
  check(wait(first, a) == gneiss::engine::result::invalid_argument && a == b,
        "async failure preserves output");
  first.cancel();
  source->byte = 'e';
  source->change = true;
  check(assets.load("asset://value", first, true) == gneiss::engine::result::success,
        "changing source");
  check(wait(first, a) == gneiss::engine::result::success && a->byte == 'e',
        "source read once without revalidation");
  b = a;
  first.cancel();
  source->change = false;
  source->missing = true;
  check(assets.load("asset://value", first, true) == gneiss::engine::result::success,
        "missing admitted");
  check(wait(first, a) == gneiss::engine::result::not_found, "missing error");
  first.cancel();
  source->missing = false;
  source->oversized = true;
  check(assets.load("asset://value", first, true) == gneiss::engine::result::success,
        "oversized admitted");
  check(wait(first, a) == gneiss::engine::result::invalid_argument, "size limit");
  first.cancel();
  source->oversized = false;
  check(assets.load("asset://value", first) == gneiss::engine::result::success,
        "cache after failures");
  check(wait(first, a) == gneiss::engine::result::success && a == b,
        "failures retain previous cache");
  source->byte = 'z';
  check(assets.load("asset://value", first, true) == gneiss::engine::result::success, "cancel all");
  first.cancel();
  drive();
  check(assets.load("asset://value", first) == gneiss::engine::result::success,
        "cache after cancellation");
  check(wait(first, a) == gneiss::engine::result::success && a == b,
        "cancelled work not published");
  check(assets.load("asset://value", first, true) == gneiss::engine::result::success,
        "close pending");
  assets.stop_resources();
  check(first.take(a) == gneiss::engine::result::invalid_state && a == b,
        "close terminates observers");
  check(assets.load("asset://value", second) == gneiss::engine::result::invalid_state,
        "closed rejects");
  check(executor.stats().retained == 0 && executor.stats().running == 0, "scope drained");
  check(a->byte == 'e', "reference survives shutdown");

  task_scheduler stopped({.workers = 0, .mode = execution_mode::cooperative});
  asset_manager rejected(gneiss::test_support::test_files(files));
  check(rejected.register_decoder(parser) == gneiss::engine::result::success, "rejection parser");
  rejected.attach_task_executor(stopped);
  stopped.request_stop();
  check(rejected.load("asset://value", first) == gneiss::engine::result::success,
        "rejection is terminal request");
  for (unsigned i = 0; i < 16 && first.take(a) == result::not_ready; ++i)
    advance_assets_for_test(rejected);
  check(first.take(a) == gneiss::engine::result::invalid_state, "queue rejection does not hang");
}
void stored_value_supersedes_pending() {
  auto& executor = gneiss::test_support::asset_tasks();
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source).ok(), "store mount");
  asset_manager assets(gneiss::test_support::test_files(files));
  check(assets.register_decoder(std::make_shared<loader>()).ok(), "store decoder");
  assets.attach_task_executor(executor);
  asset_request<value> pending;
  check(assets.load("asset://stored", pending).ok(), "pending before store");
  asset_ref<const value> stored;
  check(
      assets.store<value>("asset://stored", std::make_shared<const value>(value{'s'}), stored).ok(),
      "store value");
  asset_ref<const value> old;
  for (unsigned i = 0; i < 100 && pending.take(old) == result::not_ready; ++i) {
    advance_assets_for_test(assets);
    check(executor.run_ready().status == drive_status::success, "store drive");
  }
  check(old && old->byte == 'a', "pending observer retains its own result");
  asset_ref<const value> current;
  check(gneiss::test_support::load_asset(assets, "asset://stored", current).ok() &&
            current == stored && current->byte == 's',
        "old request cannot overwrite stored value");
  check(assets.store<value>("invalid", {}, current) == result::invalid_argument &&
            current == stored,
        "failed store preserves output");
  assets.stop_resources();
}

#if GNEISS_TEST_TASK_THREADS
void nonblocking_decode_shutdown() {
  struct gated_decoder final : asset_decoder {
    mutable std::atomic_bool entered{}, released{};
    type_id target_type() const noexcept override { return runtime_type_of<value>(); }
    std::size_t maximum_bytes() const noexcept override { return 8; }
    result decode(const std::vector<std::byte>&, std::shared_ptr<const void>& output,
                  meta::serialization_diagnostic&) const override {
      entered.store(true, std::memory_order_release);
      while (!released.load(std::memory_order_acquire))
        std::this_thread::yield();
      output = std::make_shared<const value>(value{'z'});
      return result::success;
    }
  };
  task_scheduler executor({.workers = 1, .mode = execution_mode::thread_pool});
  auto backend = std::make_shared<delayed_files>();
  auto files = std::make_shared<gneiss::engine::files::service>();
  check(files->mount({"asset://", {backend}, {}}).ok(), "stop mount");
  asset_manager assets(gneiss::test_support::test_files(files));
  auto decoder = std::make_shared<gated_decoder>();
  check(assets.register_decoder(decoder).ok(), "stop decoder");
  assets.attach_task_executor(executor);
  struct release_guard {
    gated_decoder& decoder;
    ~release_guard() { decoder.released.store(true, std::memory_order_release); }
  } release{*decoder};
  asset_request<value> request;
  check(assets.load("asset://busy", request).ok(), "stop load");
  backend->finish('z');
  advance_assets_for_test(assets);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!decoder->entered.load(std::memory_order_acquire)) {
    check(std::chrono::steady_clock::now() < deadline, "decoder started");
    std::this_thread::yield();
  }
  assets.request_stop_resources();
  assets.advance_resources();
  check(!assets.resources_stopped(), "running decode does not block stop request");
  asset_ref<const value> output;
  check(request.take(output) == result::invalid_state && !output,
        "observer terminates immediately");
  asset_request<value> rejected;
  check(assets.load("asset://other", rejected) == result::invalid_state, "stop rejects work");
  decoder->released.store(true, std::memory_order_release);
  while (!assets.resources_stopped()) {
    assets.advance_resources();
    check(std::chrono::steady_clock::now() < deadline, "stop drains after decoder exit");
    std::this_thread::yield();
  }
  check(executor.stats().retained == 0, "stop releases task scope");
  check(request.take(output) == result::invalid_state && !output, "late CPU result not published");
}
#endif

}
int main() {
  try {
    injected_file_service();
#if GNEISS_TEST_TASK_THREADS
    nonblocking_decode_shutdown();
#endif
    stored_value_supersedes_pending();
    decoder_selection();
    reference_lifetime();
    cache_collection();
    run();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
