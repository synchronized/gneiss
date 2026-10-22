// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/resource/asset_manager.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
using namespace gneiss;
using namespace gneiss::engine::resource;
using namespace gneiss::engine::core::tasks;
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct value {
  char byte{};
};
class memory_files final : public file_system {
public:
  mutable unsigned reads{};
  mutable char byte{'a'};
  mutable bool change{};
  bool missing{}, oversized{};
  gneiss_result read(std::string_view path,
                     std::vector<std::byte>& output) const noexcept override {
    return read_bounded(path, 8, output);
  }
  gneiss_result read_bounded(std::string_view, std::size_t limit,
                             std::vector<std::byte>& output) const noexcept override {
    ++reads;
    if (missing)
      return GNEISS_ERROR_NOT_FOUND;
    if (oversized || limit < 1)
      return GNEISS_ERROR_INVALID_ARGUMENT;
    try {
      output = {static_cast<std::byte>(byte)};
      if (change)
        ++byte;
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
};
class loader final : public asset_loader {
public:
  mutable unsigned decodes{};
  std::type_index target_type() const noexcept override { return typeid(value); }
  std::size_t maximum_bytes() const noexcept override { return 8; }
  result decode(const std::vector<std::byte>& bytes,
                std::shared_ptr<const void>& output) const override {
    ++decodes;
    if (bytes.empty() || bytes[0] == std::byte{'!'})
      return result::invalid_argument;
    output = std::make_shared<const value>(value{static_cast<char>(bytes[0])});
    return result::success;
  }
};
void run() {
  auto source = std::make_shared<memory_files>();
  virtual_file_system files;
  check(files.mount("asset://", source) == GNEISS_SUCCESS, "mount");
  task_scheduler executor({.workers = 0, .mode = execution_mode::cooperative});
  auto parser = std::make_shared<loader>();
  asset_manager assets(files);
  check(assets.register_loader(parser) == result::success, "register");
  check(assets.register_loader(parser) == result::invalid_argument, "duplicate registration");
  asset_request<value> first, second;
  check(assets.load("asset://value", first) == result::not_ready, "executor required");
  assets.attach_task_executor(executor);
  auto drive = [&] {
    check(executor.run_ready().status == drive_status::success, "drive");
    assets.advance_resources();
  };
  auto wait = [&](asset_request<value>& request, std::shared_ptr<const value>& output) {
    for (unsigned i = 0; i < 100; ++i) {
      const auto status = request.take(output);
      if (status != result::not_ready)
        return status;
      drive();
    }
    throw std::runtime_error("request never settled");
  };
  check(assets.load("asset://value", first) == result::success, "first");
  check(source->reads == 0, "request does not perform inline I/O");
  check(assets.load("asset://value", second) == result::success, "second");
  std::shared_ptr<const value> a, b;
  check(first.take(a) == result::not_ready, "initial pending");
  check(wait(first, a) == result::success && wait(second, b) == result::success, "coalesced load");
  check(a == b && a->byte == 'a' && parser->decodes == 1 && source->reads == 2,
        "one read and verification shared");
  check(first.take(b) == result::invalid_state && b == a, "consume once preserves output");
  check(assets.register_loader(std::make_shared<loader>()) == result::invalid_state, "frozen");
  source->byte = 'b';
  check(assets.load("asset://value", first) == result::success, "cached");
  check(wait(first, b) == result::success && b == a, "cache does not reload implicitly");
  check(assets.load("asset://value", first, true) == result::success, "reload");
  check(assets.load("asset://value", second, true) == result::success, "reload subscriber");
  first.cancel();
  check(wait(second, b) == result::success && b->byte == 'b' && a->byte == 'a',
        "cancel one keeps independent subscriber and old value");

  // 旧请求先执行完但延迟发布；同步新版本发布后，旧结果不能回写缓存。
  source->byte = 'c';
  check(assets.load("asset://value", first, true) == result::success, "old async");
  check(executor.run_ready().status == drive_status::success, "old work ready");
  source->byte = 'd';
  check(assets.load_sync("asset://value", b) == result::success && b->byte == 'd', "sync wins");
  check(wait(first, a) == result::success && a->byte == 'c', "old subscriber receives old result");
  check(assets.load("asset://value", first) == result::success, "cached after old completion");
  check(wait(first, a) == result::success && a == b, "old async cannot replace sync cache");

  source->byte = 'x';
  check(assets.load("asset://new", first) == result::success, "first-load overlap");
  check(executor.run_ready().status == drive_status::success, "first-load work ready");
  source->byte = 'y';
  std::shared_ptr<const value> independent;
  check(assets.load_sync("asset://new", independent) == result::success, "first-load sync");
  std::shared_ptr<const value> old;
  check(wait(first, old) == result::success && old->byte == 'x', "first-load old observer");
  check(assets.load("asset://new", first) == result::success, "first-load current");
  check(wait(first, old) == result::success && old == independent, "first-load order");
  source->byte = 'z';
  check(assets.load("asset://new", first, true) == result::success, "async before failed sync");
  check(executor.run_ready().status == drive_status::success, "reload work ready");
  source->byte = '!';
  check(assets.load_sync("asset://new", independent) == result::invalid_argument &&
            independent->byte == 'y',
        "failed sync preserves reference");
  check(wait(first, old) == result::success && old->byte == 'z',
        "failed sync retains async rights");
  check(assets.load("asset://new", first) == result::success, "cache after failed sync");
  check(wait(first, independent) == result::success && independent == old,
        "async published after failed sync");

  source->byte = '!';
  check(assets.load_sync("asset://value", a) == result::invalid_argument && a == b,
        "sync failure preserves output");
  check(assets.load("asset://value", first, true) == result::success, "invalid decode");
  check(wait(first, a) == result::invalid_argument && a == b, "async failure preserves output");
  first.cancel();
  source->byte = 'e';
  source->change = true;
  check(assets.load("asset://value", first, true) == result::success, "changing source");
  check(wait(first, a) == result::io, "source mismatch rejected");
  first.cancel();
  source->change = false;
  source->missing = true;
  check(assets.load("asset://value", first, true) == result::success, "missing admitted");
  check(wait(first, a) == result::not_found, "missing error");
  first.cancel();
  source->missing = false;
  source->oversized = true;
  check(assets.load("asset://value", first, true) == result::success, "oversized admitted");
  check(wait(first, a) == result::invalid_argument, "size limit");
  first.cancel();
  source->oversized = false;
  check(assets.load("asset://value", first) == result::success, "cache after failures");
  check(wait(first, a) == result::success && a == b, "failures retain previous cache");
  source->byte = 'z';
  check(assets.load("asset://value", first, true) == result::success, "cancel all");
  first.cancel();
  drive();
  check(assets.load("asset://value", first) == result::success, "cache after cancellation");
  check(wait(first, a) == result::success && a == b, "cancelled work not published");
  check(assets.load("asset://value", first, true) == result::success, "close pending");
  assets.stop_resources();
  check(first.take(a) == result::invalid_state && a == b, "close terminates observers");
  check(assets.load("asset://value", second) == result::invalid_state, "closed rejects");
  check(executor.stats().retained == 0 && executor.stats().running == 0, "scope drained");
  check(a->byte == 'd', "reference survives shutdown");

  task_scheduler stopped({.workers = 0, .mode = execution_mode::cooperative});
  asset_manager rejected(files);
  check(rejected.register_loader(parser) == result::success, "rejection parser");
  rejected.attach_task_executor(stopped);
  stopped.request_stop();
  check(rejected.load("asset://value", first) == result::success, "rejection is terminal request");
  rejected.advance_resources();
  check(first.take(a) == result::invalid_state, "queue rejection does not hang");
}
}
int main() {
  try {
    run();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
