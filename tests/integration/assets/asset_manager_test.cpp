// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_asset.hpp"
#include "../../support/memory_file_fixture.hpp"
#include "../../support/value_result.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/core/serialization/reader.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/core/types/type_id.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/platform/files/backend.hpp>
#include <gneiss/engine/platform/files/service.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/material/material_description.hpp>
#include <stdexcept>
#include <thread>

namespace gneiss::test_support {
// asset 不是首个多态基类，缓存转换必须正确调整地址并共享控制块。
struct asset_prefix {
  virtual ~asset_prefix() = default;
  std::uint64_t marker{0x12345678U};
};
struct asset_value : asset_prefix, gneiss::asset {
  asset_value(char value = {}) : asset(gneiss::runtime_type_of<asset_value>()), byte(value) {}
  asset_value(const asset_value& other) : asset_value(other.byte) {}
  asset_value& operator=(const asset_value& other) {
    byte = other.byte;
    return *this;
  }

  static constexpr auto type_guid = gneiss::guid::from_bytes({0x98, 1});
  static constexpr std::string_view type_name = "test_asset";
  char byte{};
};
}
namespace {
void advance_assets_for_test(gneiss::asset_manager& assets) {
  if (const auto files = assets.async_files())
    (void)files->dispatch_completions(64);
  assets.advance();
}
using namespace gneiss;
using namespace gneiss;
using namespace gneiss;
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
using value = gneiss::test_support::asset_value;
struct unreflected_value : asset {
  unreflected_value(char value = {}) : asset(runtime_type_of<unreflected_value>()), byte(value) {}
  unreflected_value(const unreflected_value& other) : unreflected_value(other.byte) {}

  char byte{};
};
class memory_files final : public gneiss::test_support::memory_file_fixture {
public:
  mutable unsigned reads{};
  mutable char byte{'a'};
  mutable bool change{};
  bool missing{}, oversized{}, json{};
  std::vector<std::byte> contents;
  gneiss::error read(std::string_view path,
                     std::vector<std::byte>& output) const noexcept override {
    return read_bounded(path, 8, output);
  }
  gneiss::error read_bounded(std::string_view, std::size_t limit,
                             std::vector<std::byte>& output) const noexcept override {
    ++reads;
    if (missing)
      return gneiss::error::not_found;
    if (oversized || limit < 1 || contents.size() > limit)
      return gneiss::error::invalid_argument;
    try {
      output = contents.empty() ? std::vector{static_cast<std::byte>(byte)} : contents;
      if (json && contents.empty())
        output = {std::byte{'"'}, static_cast<std::byte>(byte), std::byte{'"'}};
      if (change)
        ++byte;
      return gneiss::error::success;
    } catch (...) {
      return gneiss::error::out_of_memory;
    }
  }
};
class loader final : public asset_decoder {
public:
  mutable unsigned decodes{};
  gneiss::type_id target_type() const noexcept override { return gneiss::runtime_type_of<value>(); }
  result<std::shared_ptr<const void>, serialization::diagnostic>
  decode(const std::vector<std::byte>& bytes) const override {
    ++decodes;
    if (bytes.empty() || bytes[0] == std::byte{'!'})
      return serialization::diagnostic{.error = error::invalid_argument};
    return std::shared_ptr<const void>{
        std::make_shared<const value>(value{static_cast<char>(bytes[0])})};
  }
};
result<value, serialization::diagnostic> explicit_decode(std::span<const std::byte> bytes) {
  if (bytes.front() == std::byte{'!'})
    return serialization::diagnostic{.error = error::invalid_argument};
  return value{'x'};
}
class delayed_files final : public gneiss::files::backend {
public:
  gneiss::files::callback<gneiss::files::bytes> pending;
  std::size_t last_read_limit{};
  error read(std::string_view, gneiss::files::read_options options,
             gneiss::files::callback<gneiss::files::bytes> callback) override {
    last_read_limit = options.max_bytes;
    pending = std::move(callback);
    return error::success;
  }
  void finish(char value) {
    auto callback = std::move(pending);
    callback({error::success, {static_cast<std::byte>(value)}});
  }
};
void injected_file_service() {
  auto backend = std::make_shared<delayed_files>();
  auto files = std::make_shared<gneiss::files::service>(
      gneiss::files::service_options{1, 512U * 1024U * 1024U, 512U * 1024U * 1024U});
  check(files->mount({"asset://", {backend}, {}}).ok(), "service mount");
  {
    error initialized = error::not_ready;
    check(files->initialize([&](error status) { initialized = status; }).ok(), "initialize");
    check(files->dispatch_completions(64).ok() && initialized.ok(), "initialized");
  }
  auto& executor = gneiss::test_support::asset_tasks();
  auto assets = std::make_unique<asset_manager>(gneiss::test_support::test_files(files));
  auto identity = assets->identify("asset://value", 1);
  auto same_identity = assets->identify("asset://value", 1);
  check(identity && same_identity && *identity == *same_identity &&
            identity->uri == "asset://value" && identity->type == 1,
        "identity returned by value");
  auto invalid_identity = assets->identify("asset://value", 0);
  check(!invalid_identity && invalid_identity.error() == error::invalid_state,
        "identity failure has no value");
  auto decoder = std::make_shared<loader>();
  check(assets->register_decoder(decoder).ok(), "service decoder");
  assets->configure(executor);
  asset_request<value> first, second;
  asset_ref<const value> loaded, other;
  check(gneiss::test_support::assign_value(
            assets->find<
                std::remove_const_t<typename std::remove_cvref_t<decltype(loaded)>::element_type>>(
                "asset://missing"),
            loaded) == error::not_found,
        "cache miss does not trigger IO");
  check(
      gneiss::test_support::assign_value(
          assets->load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
          first)
          .ok(),
      "service request");
  check(
      gneiss::test_support::assign_value(
          assets->load<typename std::remove_cvref_t<decltype(second)>::value_type>("asset://value"),
          second)
          .ok(),
      "service coalescing");
  asset_request<value> full;
  check(gneiss::test_support::assign_value(
            assets->load<typename std::remove_cvref_t<decltype(full)>::value_type>("asset://other"),
            full)
                .ok() &&
            full.status() == error::not_ready,
        "file capacity queues accepted operation");
  for (unsigned i = 0; i < 8; ++i)
    advance_assets_for_test(*assets);
  check(executor.stats().submitted == 0 && decoder->decodes == 0,
        "IO waiting submits no CPU polling task");
  check(backend->last_read_limit == 512U * 1024U * 1024U,
        "loading uses configured file limit without a 64/256 MiB cap");
  full.cancel();
  advance_assets_for_test(*assets);
  check(executor.stats().submitted == 0, "cancel queued read without decoding");
  backend->finish('q');
  advance_assets_for_test(*assets);
  check(executor.stats().submitted == 1 && decoder->decodes == 0,
        "one decode after file completion");
  check(executor.run_ready().status == gneiss::tasks::drive_status::success, "decode task");
  advance_assets_for_test(*assets);
  check(gneiss::test_support::take_request(first, loaded).ok() &&
            gneiss::test_support::take_request(second, other).ok() && loaded == other &&
            loaded->byte == 'q',
        "shared decoded result");
  check(gneiss::test_support::assign_value(
            assets->find<
                std::remove_const_t<typename std::remove_cvref_t<decltype(other)>::element_type>>(
                "asset://value"),
            other)
                .ok() &&
            other == loaded,
        "service cache readable synchronously");
  check(gneiss::test_support::assign_value(
            assets->find<
                std::remove_const_t<typename std::remove_cvref_t<decltype(other)>::element_type>>(
                "asset://missing"),
            other) == error::not_found &&
            other == loaded,
        "cache miss preserves output");
  std::shared_ptr<const gneiss::files::source_domain> source;
  check(files->resolve_source("asset://value", source).ok() && source &&
            assets->cached_count() == 1,
        "service cache source identity");
  check(gneiss::test_support::assign_value(
            assets->reload<typename std::remove_cvref_t<decltype(first)>::value_type>(
                "asset://value"),
            first)
            .ok(),
        "cancel service load");
  first.cancel();
  // 请求与实际使用者全部放弃后才取消；保留资产引用意味着仍需要加载。
  loaded.reset();
  other.reset();
  advance_assets_for_test(*assets);
  backend->finish('r');
  advance_assets_for_test(*assets);
  check(executor.stats().submitted == 1, "cancelled IO never decodes");
  check(gneiss::test_support::assign_value(
            assets->find<
                std::remove_const_t<typename std::remove_cvref_t<decltype(other)>::element_type>>(
                "asset://value"),
            other) == error::not_found &&
            !other,
        "all owners released leaves no strong cache");
  check(gneiss::test_support::assign_value(
            assets->load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://late"),
            first)
            .ok(),
        "late request");
  assets->request_stop();
  check(!assets->stopped(), "stop completion requires owner advance");
  assets->advance();
  check(assets->stopped(), "manager stops without waiting for file callback");
  assets.reset();
  check(gneiss::test_support::take_request(first, other) == error::invalid_state && other == loaded,
        "stop terminates observer");
  backend->finish('s');
  check(files->dispatch_completions(64).ok() && executor.stats().submitted == 1,
        "late IO cannot access destroyed manager");
  bool closed = false;
  check(files->shutdown([&](error status) { closed = status.ok(); }).ok(),
        "shared service shutdown");
  check(files->dispatch_completions(64).ok() && closed, "shared service drained");
}
error register_test_assets() {
  gneiss::serialization::content_codec codec;
  codec.decode = [](const serialization::read_context&, serialization::reader& input,
                    const void* node) -> result<ref<void>, serialization::diagnostic> {
    if (!input.is_string(node) || input.string(node) == "?")
      return serialization::diagnostic{7U, "/value", "invalid value", error::invalid_argument};
    return ref<void>{std::make_shared<value>(value{'r'})};
  };
  return meta::registration::type<value>()
      .guid(guid::from_bytes({0x98, 1}))
      .name("test_asset")
      .metadata(codec)
      .commit();
}
void decoder_selection() {
  auto& executor = gneiss::test_support::asset_tasks();
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == error::success, "decoder mount");
  source->json = true;
  const auto adopted_scope = executor.make_scope();
  {
    asset_manager scoped(gneiss::test_support::test_files(files));
    gneiss::tasks::task_scheduler foreign({.mode = gneiss::tasks::execution_mode::cooperative});
    const auto foreign_scope = foreign.make_scope();
    check(scoped.configure(executor, {}, foreign_scope) == error::invalid_argument,
          "foreign scope is rejected without ownership transfer");
    check(foreign.close_scope(foreign_scope), "caller retains rejected scope");
    check(scoped.configure(executor, {}, adopted_scope).ok(), "adopt independent scope");
    check(scoped.configure(executor).ok(), "adopted configuration is idempotent");
  }
  check(!executor.idle(adopted_scope), "manager destruction closes adopted scope");
  asset_manager reflected(gneiss::test_support::test_files(files));
  asset_ref<const value> output;
  check(gneiss::test_support::load_asset(reflected, "asset://value", output, true) ==
                error::success &&
            output->byte == 'r',
        "reflection fallback uses default registry");
  const auto cached_reads = source->reads;
  auto cached_output = output;
  check(gneiss::test_support::load_asset(reflected, "asset://value", output) == error::success &&
            output == cached_output && source->reads == cached_reads,
        "load reuses cache");
  auto previous = output;
  source->byte = '?';
  gneiss::serialization::diagnostic failure;
  check(gneiss::test_support::load_asset(reflected, "asset://value", output, true, &failure) ==
                error::invalid_argument &&
            output == previous,
        "reflection failure preserves output");
  check(failure.byte_offset == 7U && failure.path == "/value" && failure.message == "invalid value",
        "load diagnostic");
  check(reflected.register_decoder<value>(&explicit_decode) == error::invalid_state,
        "fallback freezes registration");
  source->byte = 'a';
  check(reflected.configure(executor).ok(), "configure CPU-only manager");
  check(reflected.configure(executor).ok(), "same configuration is idempotent");
  check(reflected.configure(executor, texture_prepare_profile{.generation = 1}) ==
            error::invalid_state,
        "configuration conflict is rejected");
  gneiss::tasks::task_scheduler other_executor(
      {.workers = 0, .mode = gneiss::tasks::execution_mode::cooperative});
  check(reflected.configure(other_executor) == error::invalid_state,
        "manager rejects executor replacement");
  asset_request<value> request;
  check(gneiss::test_support::assign_value(
            reflected.load<typename std::remove_cvref_t<decltype(request)>::value_type>(
                "asset://async"),
            request) == error::success,
        "async reflection request");
  auto async_status = gneiss::test_support::take_request(request, output);
  for (unsigned i = 0; i != 32 && async_status == error::not_ready; ++i) {
    check(executor.run_ready().status == gneiss::tasks::drive_status::success, "reflection drive");
    advance_assets_for_test(reflected);
    async_status = gneiss::test_support::take_request(request, output);
  }
  check(async_status == error::success && output->byte == 'r', "async reflection decode");
  source->byte = '?';
  previous = output;
  check(gneiss::test_support::assign_value(
            reflected.reload<typename std::remove_cvref_t<decltype(request)>::value_type>(
                "asset://async"),
            request) == error::success,
        "async failure request");
  async_status = error::not_ready;
  for (unsigned i = 0; i != 32 && async_status == error::not_ready; ++i) {
    check(executor.run_ready().status == gneiss::tasks::drive_status::success, "failure drive");
    advance_assets_for_test(reflected);
    async_status = gneiss::test_support::take_request(request, output, &failure);
  }
  check(async_status == error::invalid_argument && output == previous &&
            failure.byte_offset == 7U && failure.path == "/value" &&
            failure.message == "invalid value",
        "async diagnostic and unchanged output");
  source->byte = 'a';
  // 同一个类型同时具备反射与显式解码器时，只采用显式函数。
  source->json = false;
  asset_manager explicit_assets(gneiss::test_support::test_files(files));
  check(explicit_assets.register_decoder<value>(&explicit_decode) == error::success,
        "typed decoder");
  check(explicit_assets.register_decoder<value>(&explicit_decode) == error::invalid_argument,
        "duplicate decoder");
  check(gneiss::test_support::load_asset(explicit_assets, "asset://explicit", output, true) ==
                error::success &&
            output->byte == 'x',
        "explicit decoder wins");
  previous = output;
  source->byte = '!';
  check(gneiss::test_support::load_asset(explicit_assets, "asset://explicit", output, true) ==
                error::invalid_argument &&
            output == previous,
        "explicit failure does not fall back");
  asset_manager unknown(gneiss::test_support::test_files(files));
  const auto reads = source->reads;
  asset_ref<const unreflected_value> unknown_output;
  check(gneiss::test_support::load_asset(unknown, "asset://unknown", unknown_output) ==
                error::unsupported &&
            source->reads == reads,
        "missing decoder fails before IO");
  const auto cache_before_rejection = unknown.cached_count();
  check(unknown.load<std::string>("asset://plain").error() == error::unsupported &&
            unknown.find<std::string>("asset://plain").error() == error::unsupported &&
            unknown.store<std::string>("asset://plain", std::make_shared<const std::string>("x"))
                    .error() == error::unsupported &&
            source->reads == reads && unknown.cached_count() == cache_before_rejection,
        "plain values cannot enter the asset index");
  asset_manager native(gneiss::test_support::test_files(files));
  check(
      native.register_decoder<unreflected_value>(
          +[](std::span<const std::byte>) -> result<unreflected_value, serialization::diagnostic> {
            return unreflected_value{'x'};
          }) == error::success,
      "unreflected decoder registration");
  source->byte = 'a';
  asset_ref<const unreflected_value> native_output;
  check(gneiss::test_support::load_asset(native, "asset://native", native_output) ==
                error::success &&
            native_output->byte == 'x',
        "unreflected type loads");
  material_description description;
  description.parameters.metallic = 0.25F;
  gneiss::serialization::diagnostic diagnostic;
  check(gneiss::test_support::assign_decoded(gneiss::asset_file::serialize_bytes(description),
                                             source->contents, diagnostic) == error::success,
        "material fixture");
  gneiss::render::service material_render;
  asset_manager material_assets(gneiss::test_support::test_files(files));
  check(material_assets.attach_render(material_render).ok(), "material render");
  ref<const gneiss::material> material;
  check(gneiss::test_support::load_asset(material_assets, "asset://material", material) ==
                error::success &&
            material->parameters().metallic == 0.25F,
        "builtin material reflection load");
  auto limited_files =
      std::make_shared<gneiss::files::service>(gneiss::files::service_options{.max_file_bytes = 1});
  auto limited_source = std::make_shared<memory_files>();
  limited_source->contents = source->contents;
  check(limited_files->mount({"asset://", {limited_source}, {}}).ok(), "limited mount");
  {
    error initialized = error::not_ready;
    if (limited_files->initialize([&](error status) { initialized = status; }).failed() ||
        limited_files->dispatch_completions(64).failed() || initialized.failed())
      throw std::runtime_error("文件服务初始化失败");
  }
  asset_manager limited(limited_files);
  check(limited.attach_render(material_render).ok(), "limited render");
  auto saved = material;
  check(gneiss::test_support::load_asset(limited, "asset://material", material) ==
                error::invalid_argument &&
            material == saved,
        "file service input limit preserves output");
  reflected.stop();
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
  check(files.mount("asset://", source) == gneiss::error::success, "collection mount");
  auto& executor = gneiss::test_support::asset_tasks();
  asset_manager assets(gneiss::test_support::test_files(files));
  check(assets.register_decoder(std::make_shared<loader>()) == gneiss::error::success,
        "collection loader");
  assets.configure(executor);
  asset_ref<const value> held;
  check(gneiss::test_support::load_asset(assets, "asset://value", held) == gneiss::error::success,
        "seed cache");
  check(assets.cached_count() == 1U, "CPU result uses unified index");
  assets.collect_unused();
  asset_request<value> request;
  check(
      gneiss::test_support::assign_value(
          assets.load<typename std::remove_cvref_t<decltype(request)>::value_type>("asset://value"),
          request) == gneiss::error::success,
      "cached load");
  asset_ref<const value> copy;
  check(gneiss::test_support::take_request(request, copy) == gneiss::error::success &&
            held == copy && source->reads == 1,
        "external reference prevents collection");
  held.reset();
  copy.reset();
  check(assets.find<value>("asset://value").error() == gneiss::error::not_found,
        "weak CPU cache expires before collection");
  assets.collect_unused();
  check(assets.cached_count() == 0U, "CPU entry collected after last reference");
  source->byte = 'b';
  check(
      gneiss::test_support::assign_value(
          assets.load<typename std::remove_cvref_t<decltype(request)>::value_type>("asset://value"),
          request) == gneiss::error::success,
      "load after collection");
  // 取消观察者后，活动任务仍必须拥有完成所需的索引。
  request.cancel();
  assets.collect_unused();
  check(executor.run_ready().status == gneiss::tasks::drive_status::success,
        "cancelled work completes");
  advance_assets_for_test(assets);
  assets.collect_unused();
  check(
      gneiss::test_support::assign_value(
          assets.load<typename std::remove_cvref_t<decltype(request)>::value_type>("asset://value"),
          request) == gneiss::error::success,
      "retry collected load");
  for (unsigned i = 0; i < 10 && !copy; ++i) {
    check(executor.run_ready().status == gneiss::tasks::drive_status::success, "collection drive");
    advance_assets_for_test(assets);
    assets.collect_unused();
    const auto status = gneiss::test_support::take_request(request, copy);
    check(status == gneiss::error::success || status == gneiss::error::not_ready,
          "collection preserves active request");
  }
  check(copy && copy->byte == 'b', "new content after collection");
  source->byte = '!';
  check(gneiss::test_support::assign_value(
            assets.reload<typename std::remove_cvref_t<decltype(request)>::value_type>(
                "asset://value"),
            request) == gneiss::error::success,
        "reload before collection");
  copy.reset();
  assets.collect_unused();
  check(assets.cached_count() == 1U, "in-flight reload retains previous cache entry");
  auto reload_status = gneiss::error::not_ready;
  for (unsigned i = 0; i < 10 && reload_status == gneiss::error::not_ready; ++i) {
    check(executor.run_ready().status == gneiss::tasks::drive_status::success,
          "failed reload drive");
    advance_assets_for_test(assets);
    reload_status = gneiss::test_support::take_request(request, copy);
  }
  check(reload_status == gneiss::error::invalid_argument, "reload fails without replacement");
  auto retained_failure = assets.find<value>("asset://value");
  check(retained_failure && (*retained_failure)->byte == 'b',
        "failed request retains its resource identity and previous content");
  retained_failure->reset();
  request.cancel();
  auto expired_failure = assets.find<value>("asset://value");
  check(!expired_failure && expired_failure.error() == error::not_found,
        "releasing failed observer expires otherwise unowned identity");
  source->byte = 'b';
  check(gneiss::test_support::load_asset(assets, "asset://value", copy) == gneiss::error::success,
        "reload after weak value expired");
  check(
      gneiss::test_support::assign_value(
          assets.load<typename std::remove_cvref_t<decltype(request)>::value_type>("asset://value"),
          request) == gneiss::error::success &&
          gneiss::test_support::take_request(request, copy) == gneiss::error::success &&
          copy->byte == 'b',
      "newly loaded content is reused while owned");
  assets.stop();
  check(copy->byte == 'b', "reference survives manager stop");
}
void run() {
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::error::success, "mount");
  auto& executor = gneiss::test_support::asset_tasks();
  auto parser = std::make_shared<loader>();
  asset_manager assets(gneiss::test_support::test_files(files));
  check(assets.register_decoder(parser) == gneiss::error::success, "register");
  check(assets.register_decoder(parser) == gneiss::error::invalid_argument,
        "duplicate registration");
  asset_request<value> first, second;
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
            first) == gneiss::error::not_ready,
        "executor required");
  assets.configure(executor);
  auto drive = [&] {
    check(executor.run_ready().status == gneiss::tasks::drive_status::success, "drive");
    advance_assets_for_test(assets);
  };
  auto wait = [&](asset_request<value>& request, asset_ref<const value>& output) {
    for (unsigned i = 0; i < 100; ++i) {
      const auto status = gneiss::test_support::take_request(request, output);
      if (status != gneiss::error::not_ready)
        return status;
      drive();
    }
    throw std::runtime_error("request never settled");
  };
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
            first) == gneiss::error::success,
        "first");
  check(source->reads == 0, "request does not perform inline I/O");
  check(
      gneiss::test_support::assign_value(
          assets.load<typename std::remove_cvref_t<decltype(second)>::value_type>("asset://value"),
          second) == gneiss::error::success,
      "second");
  asset_ref<const value> a, b;
  check(gneiss::test_support::take_request(first, a) == gneiss::error::not_ready,
        "initial pending");
  check(wait(first, a) == gneiss::error::success && wait(second, b) == gneiss::error::success,
        "coalesced load");
  check(a == b && a->byte == 'a' && parser->decodes == 1 && source->reads == 1,
        "one read and read and decode shared");
  check(gneiss::test_support::take_request(first, b) == gneiss::error::invalid_state && b == a,
        "consume once preserves output");
  check(assets.register_decoder(std::make_shared<loader>()) == gneiss::error::invalid_state,
        "frozen");
  source->byte = 'b';
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
            first) == gneiss::error::success,
        "cached");
  check(wait(first, b) == gneiss::error::success && b == a, "cache does not reload implicitly");
  check(
      gneiss::test_support::assign_value(
          assets.reload<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
          first) == gneiss::error::success,
      "reload");
  check(gneiss::test_support::assign_value(
            assets.reload<typename std::remove_cvref_t<decltype(second)>::value_type>(
                "asset://value"),
            second) == gneiss::error::success,
        "reload subscriber");
  first.cancel();
  check(wait(second, b) == gneiss::error::success && b->byte == 'b' && a->byte == 'a',
        "cancel one keeps independent subscriber and old value");

  // 旧请求解码完但尚未分发；显式采用新数据后，旧结果不能回写缓存。
  source->byte = 'c';
  check(
      gneiss::test_support::assign_value(
          assets.reload<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
          first) == gneiss::error::success,
      "old async");
  advance_assets_for_test(assets); // 文件快照先交给异步来源，解码尚未发布。
  check(executor.run_ready().status == gneiss::tasks::drive_status::success, "old work ready");
  source->byte = 'd';
  check(gneiss::test_support::assign_value(
            assets.store<value>("asset://value", std::make_shared<const value>(value{'d'})), b) ==
                gneiss::error::success &&
            b->byte == 'd',
        "store wins");
  check(wait(first, a) == error::invalid_state && a->byte == 'a',
        "superseded reload cannot adopt or change output");
  assets.collect_unused(); // 失效操作收尾后，清理不能移除仍被持有的新缓存。
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
            first) == gneiss::error::success,
        "cached after old completion");
  check(wait(first, a) == gneiss::error::success && a == b,
        "old async cannot replace stored cache");

  source->byte = 'x';
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://new"),
            first) == gneiss::error::success,
        "first-load overlap");
  advance_assets_for_test(assets); // 文件快照先交给异步来源，解码尚未发布。
  check(executor.run_ready().status == gneiss::tasks::drive_status::success,
        "first-load work ready");
  source->byte = 'y';
  asset_ref<const value> independent;
  check(gneiss::test_support::assign_value(
            assets.store<value>("asset://new", std::make_shared<const value>(value{'y'})),
            independent) == gneiss::error::success,
        "first-load store");
  assets.collect_unused(); // 旧操作仍在执行，但已不拥有当前记录的发布权。
  asset_ref<const value> old;
  check(wait(first, old) == error::invalid_state && !old, "superseded first load cannot adopt");
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://new"),
            first) == gneiss::error::success,
        "first-load current");
  check(wait(first, old) == gneiss::error::success && old == independent, "first-load order");
  source->byte = 'z';
  check(gneiss::test_support::assign_value(
            assets.reload<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://new"),
            first) == gneiss::error::success,
        "async before failed store");
  advance_assets_for_test(assets); // 文件快照先交给异步来源，解码尚未发布。
  check(executor.run_ready().status == gneiss::tasks::drive_status::success, "reload work ready");
  source->byte = '!';
  check(gneiss::test_support::assign_value(assets.store<value>("asset://new", {}), independent) ==
                gneiss::error::invalid_argument &&
            independent->byte == 'y',
        "failed store preserves reference");
  check(wait(first, old) == gneiss::error::success && old->byte == 'z',
        "failed store retains async rights");
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://new"),
            first) == gneiss::error::success,
        "cache after failed store");
  check(wait(first, independent) == gneiss::error::success && independent == old,
        "async published after failed store");

  source->byte = '!';
  check(gneiss::test_support::load_asset(assets, "asset://value", a, true) ==
                gneiss::error::invalid_argument &&
            a == b,
        "load failure preserves output");
  check(
      gneiss::test_support::assign_value(
          assets.reload<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
          first) == gneiss::error::success,
      "invalid decode");
  check(wait(first, a) == gneiss::error::invalid_argument && a == b,
        "async failure preserves output");
  first.cancel();
  source->byte = 'e';
  source->change = true;
  check(
      gneiss::test_support::assign_value(
          assets.reload<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
          first) == gneiss::error::success,
      "changing source");
  check(wait(first, a) == gneiss::error::success && a->byte == 'e',
        "source read once without revalidation");
  b = a;
  first.cancel();
  source->change = false;
  source->missing = true;
  check(
      gneiss::test_support::assign_value(
          assets.reload<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
          first) == gneiss::error::success,
      "missing admitted");
  check(wait(first, a) == gneiss::error::not_found, "missing error");
  first.cancel();
  source->missing = false;
  source->oversized = true;
  check(
      gneiss::test_support::assign_value(
          assets.reload<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
          first) == gneiss::error::success,
      "oversized admitted");
  check(wait(first, a) == gneiss::error::invalid_argument, "size limit");
  first.cancel();
  source->oversized = false;
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
            first) == gneiss::error::success,
        "cache after failures");
  check(wait(first, a) == gneiss::error::success && a == b, "failures retain previous cache");
  source->byte = 'z';
  check(
      gneiss::test_support::assign_value(
          assets.reload<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
          first) == gneiss::error::success,
      "cancel all");
  first.cancel();
  drive();
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
            first) == gneiss::error::success,
        "cache after cancellation");
  check(wait(first, a) == error::success && a->byte == 'z' && b->byte == 'e',
        "resource owner keeps reload alive after request cancellation");
  b = a;
  check(
      gneiss::test_support::assign_value(
          assets.reload<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
          first) == gneiss::error::success,
      "close pending");
  assets.stop();
  check(gneiss::test_support::take_request(first, a) == gneiss::error::invalid_state && a == b,
        "close terminates observers");
  check(
      gneiss::test_support::assign_value(
          assets.load<typename std::remove_cvref_t<decltype(second)>::value_type>("asset://value"),
          second) == gneiss::error::invalid_state,
      "closed rejects");
  check(executor.stats().retained == 0 && executor.stats().running == 0, "scope drained");
  check(a->byte == 'z', "reference survives shutdown");

  gneiss::tasks::task_scheduler stopped(
      {.workers = 0, .mode = gneiss::tasks::execution_mode::cooperative});
  asset_manager rejected(gneiss::test_support::test_files(files));
  check(rejected.register_decoder(parser) == gneiss::error::success, "rejection parser");
  rejected.configure(stopped);
  stopped.request_stop();
  check(
      gneiss::test_support::assign_value(
          rejected.load<typename std::remove_cvref_t<decltype(first)>::value_type>("asset://value"),
          first) == gneiss::error::success,
      "rejection is terminal request");
  for (unsigned i = 0; i < 16 && gneiss::test_support::take_request(first, a) == error::not_ready;
       ++i)
    advance_assets_for_test(rejected);
  check(gneiss::test_support::take_request(first, a) == gneiss::error::invalid_state,
        "queue rejection does not hang");
}
void decode_capacity_retry() {
  gneiss::tasks::task_scheduler executor(
      {.capacity = 2U, .mode = gneiss::tasks::execution_mode::cooperative});
  const auto occupied = executor.make_scope();
  gneiss::tasks::task_handle blocker;
  check(executor.submit(
            {.scope = occupied}, [](const auto&) { return gneiss::tasks::task_outcome{}; },
            blocker) == gneiss::tasks::submit_result::success,
        "occupy decode capacity");
  check(executor.submit(
            {.scope = occupied}, [](const auto&) { return gneiss::tasks::task_outcome{}; },
            blocker) == gneiss::tasks::submit_result::success,
        "fill decode capacity");
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source).ok(), "retry mount");
  asset_manager assets(gneiss::test_support::test_files(files));
  auto decoder = std::make_shared<loader>();
  check(assets.register_decoder(decoder).ok(), "retry decoder");
  check(assets.configure(executor).ok(), "retry executor");
  auto submitted = assets.load<value>("asset://retry");
  check(bool(submitted), "retry request");
  advance_assets_for_test(assets);
  advance_assets_for_test(assets);
  check(submitted->status() == error::not_ready && decoder->decodes == 0,
        "full decode queue does not lose request");
  (void)executor.run_ready();
  std::vector<gneiss::tasks::task_completion> completed;
  check(executor.poll(occupied, completed) == 2, "release decode capacity");
  advance_assets_for_test(assets);
  (void)executor.run_ready();
  advance_assets_for_test(assets);
  auto loaded = submitted->take();
  check(loaded && (*loaded)->byte == 'a' && decoder->decodes == 1 && source->reads == 1,
        "capacity recovery resumes once without rereading");
  assets.stop();
  check(executor.close_scope(occupied), "retry scope close");
}

void stored_value_supersedes_pending() {
  auto& executor = gneiss::test_support::asset_tasks();
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source).ok(), "store mount");
  asset_manager assets(gneiss::test_support::test_files(files));
  check(assets.register_decoder(std::make_shared<loader>()).ok(), "store decoder");
  assets.configure(executor);
  asset_request<value> pending;
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(pending)>::value_type>(
                "asset://stored"),
            pending)
            .ok(),
        "pending before store");
  asset_ref<const value> stored;
  check(
      gneiss::test_support::assign_value(
          assets.store<value>("asset://stored", std::make_shared<const value>(value{'s'})), stored)
          .ok(),
      "store value");
  asset_ref<const value> old;
  for (unsigned i = 0;
       i < 100 && gneiss::test_support::take_request(pending, old) == error::not_ready; ++i) {
    advance_assets_for_test(assets);
    check(executor.run_ready().status == gneiss::tasks::drive_status::success, "store drive");
  }
  check(!old && pending.status() == error::invalid_state, "superseded operation cannot adopt");
  asset_ref<const value> current;
  check(gneiss::test_support::load_asset(assets, "asset://stored", current).ok() &&
            current == stored && current->byte == 's',
        "old request cannot overwrite stored value");
  check(gneiss::test_support::assign_value(assets.store<value>("invalid", {}), current) ==
                error::invalid_argument &&
            current == stored,
        "failed store preserves output");
  assets.stop();
}

#if GNEISS_TEST_TASK_THREADS
void nonblocking_decode_shutdown() {
  struct gated_decoder final : asset_decoder {
    mutable std::atomic_bool entered{}, released{};
    type_id target_type() const noexcept override { return runtime_type_of<value>(); }
    result<std::shared_ptr<const void>, serialization::diagnostic>
    decode(const std::vector<std::byte>&) const override {
      entered.store(true, std::memory_order_release);
      while (!released.load(std::memory_order_acquire))
        std::this_thread::yield();
      return std::shared_ptr<const void>{std::make_shared<const value>(value{'z'})};
    }
  };
  gneiss::tasks::task_scheduler executor(
      {.workers = 1, .mode = gneiss::tasks::execution_mode::thread_pool});
  auto backend = std::make_shared<delayed_files>();
  auto files = std::make_shared<gneiss::files::service>();
  check(files->mount({"asset://", {backend}, {}}).ok(), "stop mount");
  {
    error initialized = error::not_ready;
    if (files->initialize([&](error status) { initialized = status; }).failed() ||
        files->dispatch_completions(64).failed() || initialized.failed())
      throw std::runtime_error("文件服务初始化失败");
  }
  asset_manager assets(gneiss::test_support::test_files(files));
  auto decoder = std::make_shared<gated_decoder>();
  check(assets.register_decoder(decoder).ok(), "stop decoder");
  assets.configure(executor);
  struct release_guard {
    gated_decoder& decoder;
    ~release_guard() { decoder.released.store(true, std::memory_order_release); }
  } release{*decoder};
  asset_request<value> request;
  check(
      gneiss::test_support::assign_value(
          assets.load<typename std::remove_cvref_t<decltype(request)>::value_type>("asset://busy"),
          request)
          .ok(),
      "stop load");
  backend->finish('z');
  advance_assets_for_test(assets);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!decoder->entered.load(std::memory_order_acquire)) {
    check(std::chrono::steady_clock::now() < deadline, "decoder started");
    std::this_thread::yield();
  }
  assets.request_stop();
  assets.advance();
  check(!assets.stopped(), "running decode does not block stop request");
  asset_ref<const value> output;
  check(gneiss::test_support::take_request(request, output) == error::invalid_state && !output,
        "observer terminates immediately");
  asset_request<value> rejected;
  check(gneiss::test_support::assign_value(
            assets.load<typename std::remove_cvref_t<decltype(rejected)>::value_type>(
                "asset://other"),
            rejected) == error::invalid_state,
        "stop rejects work");
  decoder->released.store(true, std::memory_order_release);
  while (!assets.stopped()) {
    assets.advance();
    check(std::chrono::steady_clock::now() < deadline, "stop drains after decoder exit");
    std::this_thread::yield();
  }
  check(executor.stats().retained == 0, "stop releases task scope");
  check(gneiss::test_support::take_request(request, output) == error::invalid_state && !output,
        "late CPU result not published");
}
#endif

}
void request_value_contract() {
  using namespace gneiss;
  asset_request<int> empty;
  auto absent = empty.take();
  check(!absent && absent.error().error == error::invalid_state, "empty take");
  auto state = std::make_shared<detail::asset_state>();
  asset_request<int> request(state);
  auto pending = request.take();
  check(!pending && pending.error().error == error::not_ready && bool(request),
        "pending keeps request");
  state->status = error::invalid_argument;
  state->diagnostic = {17, "/value", "无效整数"};
  auto failed = request.take();
  check(!failed && failed.error().error == error::invalid_argument &&
            failed.error().byte_offset == 17 && failed.error().path == "/value" &&
            failed.error().message == "无效整数" && bool(request),
        "diagnostic belongs to failed result");
  state->status = error::success;
  state->value = std::make_shared<const int>(42);
  auto loaded = request.take();
  check(loaded && **loaded == 42 && !request, "successful take consumes request");
  auto again = request.take();
  check(!again && again.error().error == error::invalid_state, "repeat take fails");
  state.reset();
  check(**loaded == 42, "returned value owns resource");
}

int main() {
  if (gneiss::initialize_meta(register_test_assets).failed())
    return 99;
  try {
    request_value_contract();
    injected_file_service();
#if GNEISS_TEST_TASK_THREADS
    nonblocking_decode_shutdown();
#endif
    decode_capacity_retry();
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
