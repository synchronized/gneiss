// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/files/async_read_file_backend.hpp"
#include "engine/resource/files/file_service.hpp"
#include "engine/resource/files/memory_file_backend.hpp"
#include "engine/resource/vfs/file_system.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
using namespace gneiss::engine;
void check(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
void pump(file_service& service, std::size_t budget = 64) {
  check(service.dispatch_completions(budget) == result::success, "dispatch");
}
void seed(memory_file_backend& backend, std::string_view path, unsigned char value) {
  bool completed = false;
  check(backend.write(path, {std::byte{value}}, {},
                      [&](result status) {
                        check(status == result::success, "seed completed");
                        completed = true;
                      }) == result::success &&
            completed,
        "seed");
}
class controlled_backend final : public file_backend {
public:
  file_callback<file_bytes> completion;
  result immediate{result::success};
  std::size_t reads{};
  result read(std::string_view, read_options, file_callback<file_bytes> callback) override {
    ++reads;
    if (immediate != result::success)
      return immediate;
    completion = std::move(callback);
    return result::success;
  }
  void complete() {
    auto callback = std::move(completion);
    callback({result::success, {std::byte{7}}});
  }
};

void memory_and_routes() {
  auto memory = std::make_shared<memory_file_backend>();
  auto lower = std::make_shared<memory_file_backend>();
  auto special = std::make_shared<memory_file_backend>();
  seed(*lower, "only", 2);
  seed(*lower, "same", 3);
  seed(*memory, "same", 4);
  seed(*special, "same", 9);
  file_service service;
  check(service.mount({"asset://", {memory, lower}, {}}) == result::success, "read mount");
  check(service.mount({"asset://special/", {special}, {}}) == result::success, "long mount");
  check(service.mount({"user://", {memory}, memory}) == result::success, "write mount");
  check(service.mount({"user://", {memory}, {}}) == result::invalid_state, "duplicate mount");
  check(service.mount({"asset://../", {memory}, {}}) == result::invalid_argument, "invalid mount");
  file_request_id request;
  unsigned called = 0;
  check(service.read(
            "asset://only", {16},
            [&](auto completed) {
              check(completed.status == result::success &&
                        completed.data == file_bytes{std::byte{2}},
                    "fallback bytes");
              ++called;
            },
            request) == result::success &&
            called == 0,
        "deferred submission");
  pump(service);
  check(called == 0, "fallback deferred");
  pump(service);
  check(called == 1, "fallback complete");
  check(service.cancel(request) == result::invalid_handle, "retired request");
  check(service.mount({"other://", {memory}, {}}) == result::invalid_state, "frozen mounts");
  check(service.read(
            "asset://special/same", {16},
            [&](auto completed) {
              check(completed.data == file_bytes{std::byte{9}}, "longest prefix");
              ++called;
            },
            request) == result::success,
        "long read");
  check(service.read(
            "asset://same", {16},
            [&](auto completed) {
              check(completed.data == file_bytes{std::byte{4}}, "overlay priority");
              ++called;
            },
            request) == result::success,
        "overlay read");
  pump(service, 1);
  check(called == 2, "callback budget");
  pump(service, 1);
  check(called == 3, "second callback");
  for (const auto* invalid :
       {"asset://../same", "asset://a//b", "asset://%2e%2e/same", "asset://a\\b"}) {
    request = {999};
    check(service.read(
              invalid, {16}, [](auto) {}, request) == result::invalid_argument &&
              request.value == 0,
          "invalid path");
  }
  bool written = false;
  check(service.write(
            "user://dir/a", {std::byte{5}, std::byte{6}}, {},
            [&](result status) {
              check(status == result::success, "write result");
              written = true;
            },
            request) == result::success &&
            !written,
        "write deferred");
  pump(service);
  check(written, "write complete");
  check(service.read_range(
            "user://dir/a", {1, 1},
            [](auto completed) {
              check(completed.status == result::success &&
                        completed.data == file_bytes{std::byte{6}},
                    "range");
            },
            request) == result::success,
        "range submit");
  check(service.read_range(
            "user://dir/a", {2, 0},
            [](auto completed) {
              check(completed.status == result::success && completed.data.empty(), "empty EOF");
            },
            request) == result::success,
        "EOF submit");
  check(service.read_range(
            "user://dir/a", {2, 1},
            [](auto completed) {
              check(completed.status == result::invalid_argument && completed.data.empty(),
                    "range boundary");
            },
            request) == result::success,
        "boundary submit");
  check(service.read(
            "user://dir/a", {1},
            [](auto completed) {
              check(completed.status == result::invalid_argument && completed.data.empty(),
                    "size limit");
            },
            request) == result::success,
        "limit submit");
  check(service.stat(
            "user://dir/a",
            [](auto completed) {
              check(completed.status == result::success && completed.data.size == 2, "file size");
            },
            request) == result::success,
        "stat submit");
  check(service.stat(
            "user://dir/",
            [](auto completed) {
              check(completed.status == result::success &&
                        completed.data.kind == file_kind::directory && !completed.data.size,
                    "directory info");
            },
            request) == result::success,
        "directory stat");
  pump(service);
  seed(*memory, "dir/b", 7);
  std::string cursor;
  check(service.list(
            "user://dir/", {1, {}},
            [&](auto completed) {
              check(completed.status == result::success && completed.data.entries.size() == 1 &&
                        completed.data.entries[0].name == "a",
                    "first page");
              cursor = completed.data.next_cursor;
            },
            request) == result::success,
        "list submit");
  pump(service);
  check(!cursor.empty(), "next cursor");
  check(service.list(
            "user://dir/", {1, cursor},
            [](auto completed) {
              check(completed.status == result::success && completed.data.entries.size() == 1 &&
                        completed.data.entries[0].name == "b" && completed.data.next_cursor.empty(),
                    "last page");
            },
            request) == result::success,
        "next page submit");
  check(service.write(
            "user://dir/a", {}, {write_mode::create_new},
            [](result status) { check(status == result::invalid_state, "create new collision"); },
            request) == result::success,
        "create new submit");
  check(service.flush(
            "user://",
            [](result status) { check(status == result::unsupported, "volatile flush"); },
            request) == result::success,
        "flush submit");
  check(service.write(
            "asset://same", {}, {}, [](result) {}, request) == result::unsupported,
        "readonly");
  pump(service);
  bool closed = false;
  check(service.shutdown([&](result status) {
    check(status == result::success, "shutdown");
    closed = true;
  }) == result::success &&
            !closed,
        "deferred shutdown");
  pump(service);
  check(closed, "shutdown complete");
}

void pagination_and_reentry() {
  auto upper = std::make_shared<memory_file_backend>();
  auto lower = std::make_shared<memory_file_backend>();
  seed(*lower, "dir/a-x", 1);
  seed(*lower, "dir/a/child", 2);
  file_service service;
  check(service.mount({"asset://", {upper, lower}, {}}) == result::success, "paged overlay");
  file_request_id id;
  std::string cursor;
  check(service.list(
            "asset://dir/", {1, {}},
            [&](auto completed) {
              check(completed.status == result::success && completed.data.entries.size() == 1 &&
                        completed.data.entries[0].name == "a",
                    "child order differs from path order");
              cursor = completed.data.next_cursor;
            },
            id) == result::success,
        "page fallback submit");
  pump(service);
  pump(service);
  check(!cursor.empty(), "lower backend cursor");
  seed(*upper, "dir/new", 3);
  bool second = false;
  check(service.list(
            "asset://dir/", {1, cursor},
            [&](auto completed) {
              check(completed.status == result::success && completed.data.entries.size() == 1 &&
                        completed.data.entries[0].name == "a-x",
                    "cursor pins original backend");
              second = true;
            },
            id) == result::success,
        "pinned continuation");
  pump(service);
  check(second, "continued page");
  check(service.list(
            "asset://other/", {1, cursor}, [](auto) {}, id) == result::invalid_argument,
        "cursor cannot cross paths");
  unsigned calls = 0;
  check(service.read(
            "asset://dir/new", {16},
            [&](auto completed) {
              check(completed.status == result::success, "outer callback");
              ++calls;
              check(service.read(
                        "asset://dir/new", {16},
                        [&](auto inner) {
                          check(inner.status == result::success, "inner callback");
                          ++calls;
                        },
                        id) == result::success,
                    "submit from callback");
            },
            id) == result::success,
        "outer submit");
  pump(service);
  check(calls == 1, "new callback deferred to next dispatch");
  pump(service);
  check(calls == 2, "new callback delivered");
  for (const std::string& invalid : {std::string("asset://a\n"), std::string("asset://a\xFF")}) {
    check(service.read(
              invalid, {16}, [](auto) {}, id) == result::invalid_argument,
          "UTF8 and control validation");
  }
}

void lifetime() {
  auto backend = std::make_shared<controlled_backend>();
  file_service service({1, 64});
  file_service other;
  check(service.mount({"asset://", {backend}, {}}) == result::success, "delayed mount");
  file_request_id id, rejected;
  bool called = false, closed = false;
  check(service.read(
            "asset://a", {16},
            [&](auto completed) {
              check(completed.status == result::cancelled && completed.data.empty(),
                    "cancel result");
              called = true;
              check(service.dispatch_completions(1) == result::invalid_state, "reentrant dispatch");
            },
            id) == result::success,
        "delayed submit");
  check(service.read(
            "asset://b", {16}, [](auto) {}, rejected) == result::not_ready &&
            rejected.value == 0,
        "capacity");
  check(other.cancel(id) == result::invalid_handle, "cross service");
  check(service.cancel(id) == result::success, "cancel pending");
  check(service.shutdown([&](result status) {
    check(status == result::success && called, "close ordering");
    closed = true;
  }) == result::success,
        "shutdown pending");
  pump(service);
  check(!called && !closed, "wait actual cleanup");
  check(service.read(
            "asset://b", {16}, [](auto) {}, rejected) == result::invalid_state,
        "closed submit");
  backend->complete();
  pump(service, 1);
  check(called && !closed, "shutdown respects budget");
  pump(service);
  check(closed, "closed after cancellation");

  auto failure = std::make_shared<controlled_backend>();
  auto fallback = std::make_shared<controlled_backend>();
  failure->immediate = result::io;
  file_service errors;
  check(errors.mount({"asset://", {failure, fallback}, {}}) == result::success, "error mount");
  check(errors.read(
            "asset://a", {16},
            [](auto completed) { check(completed.status == result::io, "io preserved"); },
            id) == result::success,
        "error submit");
  pump(errors);
  check(fallback->reads == 0, "no IO fallback");
  failure->immediate = result::unsupported;
  check(errors.read(
            "asset://a", {16},
            [](auto completed) {
              check(completed.status == result::unsupported, "unsupported preserved");
            },
            id) == result::success,
        "unsupported submit");
  pump(errors);
  check(fallback->reads == 0, "no unsupported fallback");

#if GNEISS_TEST_TASK_THREADS && !defined(__EMSCRIPTEN__)
  auto threaded = std::make_shared<controlled_backend>();
  file_service threaded_service;
  check(threaded_service.mount({"asset://", {threaded}, {}}) == result::success, "thread mount");
  const auto owner = std::this_thread::get_id();
  bool delivered = false;
  check(threaded_service.read(
            "asset://a", {16},
            [&](auto completed) {
              check(completed.status == result::success && std::this_thread::get_id() == owner,
                    "owner callback");
              delivered = true;
            },
            id) == result::success,
        "thread submit");
  std::thread worker([&] {
    check(threaded_service.dispatch_completions(1) == result::invalid_state, "wrong thread");
    threaded->complete();
  });
  worker.join();
  check(!delivered, "worker only completes");
  pump(threaded_service);
  check(delivered, "owner delivers");
#endif

  file_callback<file_bytes> late;
  {
    auto abandoned = std::make_shared<controlled_backend>();
    file_service dying;
    check(dying.mount({"asset://", {abandoned}, {}}) == result::success, "abandon mount");
    check(dying.read(
              "asset://a", {16}, [](auto) { check(false, "destroyed owner callback"); }, id) ==
              result::success,
          "abandon submit");
    late = std::move(abandoned->completion);
  }
  late({result::success, {}});
}
struct counters {
  unsigned opens{}, reads{}, closes{};
  std::size_t maximum{}, size{150000};
  bool short_read{};
};
class read final : public read_operation {
public:
  explicit read(std::size_t size) : bytes_(size, std::byte{42}) {}
  gneiss::engine::result poll(std::span<const std::byte>& output) noexcept override {
    if (!polled_) {
      polled_ = true;
      return gneiss::engine::result::not_ready;
    }
    output = bytes_;
    return gneiss::engine::result::success;
  }

private:
  bool polled_{};
  std::vector<std::byte> bytes_;
};
class source final : public read_source {
public:
  explicit source(std::shared_ptr<counters> stats) : stats_(std::move(stats)) {}
  std::uint64_t size() const noexcept override { return stats_->size; }
  gneiss::engine::result read_at(std::uint64_t, std::span<std::byte>) const noexcept override {
    return gneiss::engine::result::internal;
  }
  gneiss::engine::result
  begin_read(std::uint64_t, std::size_t size,
             std::unique_ptr<read_operation>& output) const noexcept override {
    ++stats_->reads;
    stats_->maximum = std::max(stats_->maximum, size);
    try {
      output = std::make_unique<read>(size - (stats_->short_read ? 1U : 0U));
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
  gneiss::engine::result begin_close() const noexcept override {
    ++stats_->closes;
    return gneiss::engine::result::success;
  }
  gneiss::engine::result poll_close() const noexcept override {
    if (!closed_) {
      closed_ = true;
      return gneiss::engine::result::not_ready;
    }
    return gneiss::engine::result::success;
  }

private:
  std::shared_ptr<counters> stats_;
  mutable bool closed_{};
};
class opening final : public open_read_operation {
public:
  explicit opening(std::shared_ptr<counters> stats) : stats_(std::move(stats)) {}
  gneiss::engine::result poll(std::unique_ptr<read_source>& output) noexcept override {
    if (!polled_) {
      polled_ = true;
      return gneiss::engine::result::not_ready;
    }
    try {
      output = std::make_unique<source>(stats_);
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }

private:
  std::shared_ptr<counters> stats_;
  bool polled_{};
};
class files final : public file_system {
public:
  std::shared_ptr<counters> stats = std::make_shared<counters>();
  gneiss::engine::result
  begin_open_read(std::string_view,
                  std::unique_ptr<open_read_operation>& output) const noexcept override {
    ++stats->opens;
    try {
      output = std::make_unique<opening>(stats);
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
  gneiss::engine::result read(std::string_view, std::vector<std::byte>&) const noexcept override {
    return gneiss::engine::result::internal;
  }
};
void async_bridge() {
  auto backend = std::make_shared<files>();
  auto adapter = std::make_shared<async_read_file_backend>(backend);
  file_service service;
  check(service.mount({"asset://", {adapter}, {}}) == result::success, "adapter mount");
  file_request_id id;
  bool done = false;
  check(service.read(
            "asset://test", {150000},
            [&](auto completed) {
              check(completed.status == result::success && completed.data.size() == 150000 &&
                        backend->stats->closes == 1,
                    "async completion after close");
              done = true;
            },
            id) == result::success &&
            !done,
        "async read");
  for (unsigned i = 0; i < 32 && !done; ++i)
    pump(service);
  check(done && backend->stats->reads == 3 && backend->stats->maximum == 65536, "chunked read");
  done = false;
  check(service.read(
            "asset://test", {149999},
            [&](auto completed) {
              check(completed.status == result::invalid_argument && completed.data.empty() &&
                        backend->stats->closes == 2,
                    "oversize closes");
              done = true;
            },
            id) == result::success,
        "oversize request");
  for (unsigned i = 0; i < 32 && !done; ++i)
    pump(service);
  check(done && backend->stats->reads == 3, "oversize no reads");
  backend->stats->short_read = true;
  done = false;
  check(service.read_range(
            "asset://test", {1, 10},
            [&](auto completed) {
              check(completed.status == result::io && completed.data.empty() &&
                        backend->stats->closes == 3,
                    "short read closes");
              done = true;
            },
            id) == result::success,
        "short request");
  for (unsigned i = 0; i < 32 && !done; ++i)
    pump(service);
  check(done, "short completes");
}
} // namespace
int main() {
  try {
    memory_and_routes();
    pagination_and_reentry();
    lifetime();
    async_bridge();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
