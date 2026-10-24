// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/asset_manager.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace gneiss::test_support {
struct asset_value {
  char byte{};
};
}
namespace {
using namespace gneiss;
using namespace gneiss::engine;
using namespace gneiss::engine;
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
using value = gneiss::test_support::asset_value;
class memory_files final : public file_system {
public:
  mutable unsigned reads{};
  mutable char byte{'a'};
  mutable bool change{};
  bool missing{}, oversized{};
  gneiss::engine::result read(std::string_view path,
                              std::vector<std::byte>& output) const noexcept override {
    return read_bounded(path, 8, output);
  }
  gneiss::engine::result read_bounded(std::string_view, std::size_t limit,
                                      std::vector<std::byte>& output) const noexcept override {
    ++reads;
    if (missing)
      return gneiss::engine::result::not_found;
    if (oversized || limit < 1)
      return gneiss::engine::result::invalid_argument;
    try {
      output = {static_cast<std::byte>(byte)};
      if (change)
        ++byte;
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
};
class loader final : public asset_loader {
public:
  mutable unsigned decodes{};
  gneiss::engine::type_id target_type() const noexcept override {
    return gneiss::engine::runtime_type_of<value>();
  }
  std::size_t maximum_bytes() const noexcept override { return 8; }
  gneiss::engine::result decode(const std::vector<std::byte>& bytes,
                                std::shared_ptr<const void>& output) const override {
    ++decodes;
    if (bytes.empty() || bytes[0] == std::byte{'!'})
      return gneiss::engine::result::invalid_argument;
    output = std::make_shared<const value>(value{static_cast<char>(bytes[0])});
    return gneiss::engine::result::success;
  }
};
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
  virtual_file_system files;
  check(files.mount("asset://", source) == gneiss::engine::result::success, "collection mount");
  task_scheduler executor({.workers = 0, .mode = execution_mode::cooperative});
  asset_manager assets(files);
  check(assets.register_loader(std::make_shared<loader>()) == gneiss::engine::result::success,
        "collection loader");
  assets.attach_task_executor(executor);
  asset_ref<const value> held;
  check(assets.load_sync("asset://value", held) == gneiss::engine::result::success, "seed cache");
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
  assets.advance_resources();
  assets.collect_unused();
  check(assets.load("asset://value", request) == gneiss::engine::result::success,
        "retry collected load");
  for (unsigned i = 0; i < 10 && !copy; ++i) {
    check(executor.run_ready().status == drive_status::success, "collection drive");
    assets.advance_resources();
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
    assets.advance_resources();
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
  virtual_file_system files;
  check(files.mount("asset://", source) == gneiss::engine::result::success, "mount");
  task_scheduler executor({.workers = 0, .mode = execution_mode::cooperative});
  auto parser = std::make_shared<loader>();
  asset_manager assets(files);
  check(assets.register_loader(parser) == gneiss::engine::result::success, "register");
  check(assets.register_loader(parser) == gneiss::engine::result::invalid_argument,
        "duplicate registration");
  asset_request<value> first, second;
  check(assets.load("asset://value", first) == gneiss::engine::result::not_ready,
        "executor required");
  assets.attach_task_executor(executor);
  auto drive = [&] {
    check(executor.run_ready().status == drive_status::success, "drive");
    assets.advance_resources();
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
  check(assets.register_loader(std::make_shared<loader>()) == gneiss::engine::result::invalid_state,
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

  // 旧请求先执行完但延迟发布；同步新版本发布后，旧结果不能回写缓存。
  source->byte = 'c';
  check(assets.load("asset://value", first, true) == gneiss::engine::result::success, "old async");
  check(executor.run_ready().status == drive_status::success, "old work ready");
  source->byte = 'd';
  check(assets.load_sync("asset://value", b) == gneiss::engine::result::success && b->byte == 'd',
        "sync wins");
  check(wait(first, a) == gneiss::engine::result::success && a->byte == 'c',
        "old subscriber receives old result");
  check(assets.load("asset://value", first) == gneiss::engine::result::success,
        "cached after old completion");
  check(wait(first, a) == gneiss::engine::result::success && a == b,
        "old async cannot replace sync cache");

  source->byte = 'x';
  check(assets.load("asset://new", first) == gneiss::engine::result::success, "first-load overlap");
  check(executor.run_ready().status == drive_status::success, "first-load work ready");
  source->byte = 'y';
  asset_ref<const value> independent;
  check(assets.load_sync("asset://new", independent) == gneiss::engine::result::success,
        "first-load sync");
  asset_ref<const value> old;
  check(wait(first, old) == gneiss::engine::result::success && old->byte == 'x',
        "first-load old observer");
  check(assets.load("asset://new", first) == gneiss::engine::result::success, "first-load current");
  check(wait(first, old) == gneiss::engine::result::success && old == independent,
        "first-load order");
  source->byte = 'z';
  check(assets.load("asset://new", first, true) == gneiss::engine::result::success,
        "async before failed sync");
  check(executor.run_ready().status == drive_status::success, "reload work ready");
  source->byte = '!';
  check(assets.load_sync("asset://new", independent) == gneiss::engine::result::invalid_argument &&
            independent->byte == 'y',
        "failed sync preserves reference");
  check(wait(first, old) == gneiss::engine::result::success && old->byte == 'z',
        "failed sync retains async rights");
  check(assets.load("asset://new", first) == gneiss::engine::result::success,
        "cache after failed sync");
  check(wait(first, independent) == gneiss::engine::result::success && independent == old,
        "async published after failed sync");

  source->byte = '!';
  check(assets.load_sync("asset://value", a) == gneiss::engine::result::invalid_argument && a == b,
        "sync failure preserves output");
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
  asset_manager rejected(files);
  check(rejected.register_loader(parser) == gneiss::engine::result::success, "rejection parser");
  rejected.attach_task_executor(stopped);
  stopped.request_stop();
  check(rejected.load("asset://value", first) == gneiss::engine::result::success,
        "rejection is terminal request");
  rejected.advance_resources();
  check(first.take(a) == gneiss::engine::result::invalid_state, "queue rejection does not hang");
}
}
int main() {
  try {
    reference_lifetime();
    cache_collection();
    run();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
