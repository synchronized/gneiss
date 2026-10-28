// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/platform/files/file_read_stream.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/platform/files/memory_file_backend.hpp"

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
void pump(gneiss::engine::files::service& service, std::size_t budget = 64) {
  check(service.dispatch_completions(budget) == error::success, "dispatch");
}
void seed(gneiss::engine::files::memory_backend& backend, std::string_view path,
          unsigned char value) {
  bool completed = false;
  check(backend.write(path, {std::byte{value}}, {},
                      [&](error status) {
                        check(status == error::success, "seed completed");
                        completed = true;
                      }) == error::success &&
            completed,
        "seed");
}
class controlled_backend final : public gneiss::engine::files::backend {
public:
  gneiss::engine::files::callback<gneiss::engine::files::bytes> completion;
  error immediate{error::success};
  std::size_t reads{};
  error read(std::string_view, gneiss::engine::files::read_options,
             gneiss::engine::files::callback<gneiss::engine::files::bytes> callback) override {
    ++reads;
    if (immediate != error::success)
      return immediate;
    completion = std::move(callback);
    return error::success;
  }
  void complete() {
    auto callback = std::move(completion);
    callback({error::success, {std::byte{7}}});
  }
};

void memory_and_routes() {
  auto memory = std::make_shared<gneiss::engine::files::memory_backend>();
  auto lower = std::make_shared<gneiss::engine::files::memory_backend>();
  auto special = std::make_shared<gneiss::engine::files::memory_backend>();
  seed(*lower, "only", 2);
  seed(*lower, "same", 3);
  seed(*memory, "same", 4);
  seed(*special, "same", 9);
  gneiss::engine::files::service service;
  check(service.mount({"asset://", {memory, lower}, {}}) == error::success, "read mount");
  check(service.mount({"asset://special/", {special}, {}}) == error::success, "long mount");
  check(service.mount({"user://", {memory}, memory}) == error::success, "write mount");
  check(service.mount({"user://", {memory}, {}}) == error::invalid_state, "duplicate mount");
  check(service.mount({"asset://../", {memory}, {}}) == error::invalid_argument, "invalid mount");
  gneiss::engine::files::request_id request;
  unsigned called = 0;
  check(service.read(
            "asset://only", {16},
            [&](auto completed) {
              check(completed.status == error::success &&
                        completed.data == gneiss::engine::files::bytes{std::byte{2}},
                    "fallback bytes");
              ++called;
            },
            request) == error::success &&
            called == 0,
        "deferred submission");
  pump(service);
  check(called == 0, "fallback deferred");
  pump(service);
  check(called == 1, "fallback complete");
  check(service.cancel(request) == error::invalid_handle, "retired request");
  check(service.mount({"other://", {memory}, {}}) == error::invalid_state, "frozen mounts");
  check(service.read(
            "asset://special/same", {16},
            [&](auto completed) {
              check(completed.data == gneiss::engine::files::bytes{std::byte{9}}, "longest prefix");
              ++called;
            },
            request) == error::success,
        "long read");
  check(service.read(
            "asset://same", {16},
            [&](auto completed) {
              check(completed.data == gneiss::engine::files::bytes{std::byte{4}},
                    "overlay priority");
              ++called;
            },
            request) == error::success,
        "overlay read");
  pump(service, 1);
  check(called == 2, "callback budget");
  pump(service, 1);
  check(called == 3, "second callback");
  for (const auto* invalid :
       {"asset://../same", "asset://a//b", "asset://%2e%2e/same", "asset://a\\b"}) {
    request = {999};
    check(service.read(
              invalid, {16}, [](auto) {}, request) == error::invalid_argument &&
              request.value == 0,
          "invalid path");
  }
  bool written = false;
  check(service.write(
            "user://dir/a", {std::byte{5}, std::byte{6}}, {},
            [&](error status) {
              check(status == error::success, "write result");
              written = true;
            },
            request) == error::success &&
            !written,
        "write deferred");
  pump(service);
  check(written, "write complete");
  check(service.read_range(
            "user://dir/a", {1, 1},
            [](auto completed) {
              check(completed.status == error::success &&
                        completed.data == gneiss::engine::files::bytes{std::byte{6}},
                    "range");
            },
            request) == error::success,
        "range submit");
  check(service.read_range(
            "user://dir/a", {2, 0},
            [](auto completed) {
              check(completed.status == error::success && completed.data.empty(), "empty EOF");
            },
            request) == error::success,
        "EOF submit");
  check(service.read_range(
            "user://dir/a", {2, 1},
            [](auto completed) {
              check(completed.status == error::invalid_argument && completed.data.empty(),
                    "range boundary");
            },
            request) == error::success,
        "boundary submit");
  check(service.read(
            "user://dir/a", {1},
            [](auto completed) {
              check(completed.status == error::invalid_argument && completed.data.empty(),
                    "size limit");
            },
            request) == error::success,
        "limit submit");
  check(service.stat(
            "user://dir/a",
            [](auto completed) {
              check(completed.status == error::success && completed.data.size == 2, "file size");
            },
            request) == error::success,
        "stat submit");
  check(service.stat(
            "user://dir/",
            [](auto completed) {
              check(completed.status == error::success &&
                        completed.data.kind == gneiss::engine::files::kind::directory &&
                        !completed.data.size,
                    "directory info");
            },
            request) == error::success,
        "directory stat");
  pump(service);
  seed(*memory, "dir/b", 7);
  std::string cursor;
  check(service.list(
            "user://dir/", {1, {}},
            [&](auto completed) {
              check(completed.status == error::success && completed.data.entries.size() == 1 &&
                        completed.data.entries[0].name == "a",
                    "first page");
              cursor = completed.data.next_cursor;
            },
            request) == error::success,
        "list submit");
  pump(service);
  check(!cursor.empty(), "next cursor");
  check(service.list(
            "user://dir/", {1, cursor},
            [](auto completed) {
              check(completed.status == error::success && completed.data.entries.size() == 1 &&
                        completed.data.entries[0].name == "b" && completed.data.next_cursor.empty(),
                    "last page");
            },
            request) == error::success,
        "next page submit");
  check(service.write(
            "user://dir/a", {}, {gneiss::engine::files::write_mode::create_new},
            [](error status) { check(status == error::invalid_state, "create new collision"); },
            request) == error::success,
        "create new submit");
  check(service.flush(
            "user://", [](error status) { check(status == error::unsupported, "volatile flush"); },
            request) == error::success,
        "flush submit");
  check(service.write(
            "asset://same", {}, {}, [](error) {}, request) == error::unsupported,
        "readonly");
  pump(service);
  bool closed = false;
  check(service.shutdown([&](error status) {
    check(status == error::success, "shutdown");
    closed = true;
  }) == error::success &&
            !closed,
        "deferred shutdown");
  pump(service);
  check(closed, "shutdown complete");
}

void pagination_and_reentry() {
  auto upper = std::make_shared<gneiss::engine::files::memory_backend>();
  auto lower = std::make_shared<gneiss::engine::files::memory_backend>();
  seed(*lower, "dir/a-x", 1);
  seed(*lower, "dir/a/child", 2);
  gneiss::engine::files::service service;
  check(service.mount({"asset://", {upper, lower}, {}}) == error::success, "paged overlay");
  gneiss::engine::files::request_id id;
  std::string cursor;
  check(service.list(
            "asset://dir/", {1, {}},
            [&](auto completed) {
              check(completed.status == error::success && completed.data.entries.size() == 1 &&
                        completed.data.entries[0].name == "a",
                    "child order differs from path order");
              cursor = completed.data.next_cursor;
            },
            id) == error::success,
        "page fallback submit");
  pump(service);
  pump(service);
  check(!cursor.empty(), "lower backend cursor");
  seed(*upper, "dir/new", 3);
  bool second = false;
  check(service.list(
            "asset://dir/", {1, cursor},
            [&](auto completed) {
              check(completed.status == error::success && completed.data.entries.size() == 1 &&
                        completed.data.entries[0].name == "a-x",
                    "cursor pins original backend");
              second = true;
            },
            id) == error::success,
        "pinned continuation");
  pump(service);
  check(second, "continued page");
  check(service.list(
            "asset://other/", {1, cursor}, [](auto) {}, id) == error::invalid_argument,
        "cursor cannot cross paths");
  unsigned calls = 0;
  check(service.read(
            "asset://dir/new", {16},
            [&](auto completed) {
              check(completed.status == error::success, "outer callback");
              ++calls;
              check(service.read(
                        "asset://dir/new", {16},
                        [&](auto inner) {
                          check(inner.status == error::success, "inner callback");
                          ++calls;
                        },
                        id) == error::success,
                    "submit from callback");
            },
            id) == error::success,
        "outer submit");
  pump(service);
  check(calls == 1, "new callback deferred to next dispatch");
  pump(service);
  check(calls == 2, "new callback delivered");
  for (const std::string& invalid : {std::string("asset://a\n"), std::string("asset://a\xFF")}) {
    check(service.read(
              invalid, {16}, [](auto) {}, id) == error::invalid_argument,
          "UTF8 and control validation");
  }
}

void byte_budget() {
  auto backend = std::make_shared<controlled_backend>();
  gneiss::engine::files::service service({8, 64, 16});
  check(service.mount({"asset://", {backend}, {}}).ok(), "budget mount");
  gneiss::engine::files::request_id first, rejected;
  check(service.read(
            "asset://a", {17}, [](auto) {}, rejected) == error::invalid_argument &&
            rejected.value == 0,
        "request exceeds total budget");
  bool delivered{};
  check(service
            .read(
                "asset://a", {16},
                [&](auto) {
                  delivered = true;
                  check(service.read(
                                   "asset://b", {16}, [](auto) {}, first)
                            .ok(),
                        "budget released before callback");
                },
                first)
            .ok(),
        "budget reservation");
  check(service.read(
            "asset://b", {1}, [](auto) {}, rejected) == error::not_ready &&
            backend->reads == 1,
        "inflight budget prevents submission");
  backend->complete();
  check(service.read(
            "asset://b", {1}, [](auto) {}, rejected) == error::not_ready,
        "completed bytes remain reserved until delivery");
  pump(service);
  check(delivered && backend->reads == 2, "delivery admits next read");
  check(service.cancel(first).ok(), "cancel budget request");
  check(service.read(
            "asset://b", {1}, [](auto) {}, rejected) == error::not_ready,
        "cancel waits for backend before releasing budget");
  backend->complete();
  pump(service);
  check(service.read("asset://b", {16}, [](auto) {}, first).ok(), "cancelled budget released");
  backend->complete();
  pump(service);
}

void stream_blocks() {
  auto memory = std::make_shared<gneiss::engine::files::memory_backend>();
  const gneiss::engine::files::bytes bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                           std::byte{5}};
  check(memory->write("data", bytes, {}, [](auto) {}).ok(), "stream seed");
  auto service = std::make_shared<gneiss::engine::files::service>(
      gneiss::engine::files::service_options{8, 8, 2});
  check(service->mount({"asset://", {memory}, {}}).ok(), "stream mount");
  gneiss::engine::files::read_stream stream(service, "asset://data", 5, 2);
  gneiss::engine::files::bytes output;
  bool eof{};
  unsigned calls{};
  const auto next = [&](gneiss::engine::files::completion<gneiss::engine::files::block> value) {
    check(value.status.ok() && value.data.offset == output.size(), "ordered block");
    output.insert(output.end(), value.data.bytes.begin(), value.data.bytes.end());
    eof = value.data.eof;
    ++calls;
  };
  check(stream.read_next(next).ok() && calls == 0, "stream defers callback");
  check(stream.read_next(next) == error::not_ready, "one inflight block");
  pump(*service);
  check(calls == 1 && !eof, "first block");
  pump(*service);
  check(calls == 1, "no implicit prefetch");
  check(stream.read_next(next).ok(), "next block");
  pump(*service);
  check(stream.read_next(next).ok(), "last block");
  pump(*service);
  check(calls == 3 && eof && output == bytes, "stream complete");
  check(stream.read_next(next) == error::invalid_state, "EOF terminal");
  check(memory->write("empty", {}, {}, [](auto) {}).ok(), "empty seed");
  gneiss::engine::files::read_stream empty(service, "asset://empty", 0, 2);
  bool empty_done{};
  check(empty
            .read_next([&](auto value) {
              check(value.status.ok() && value.data.eof && value.data.bytes.empty(), "empty EOF");
              empty_done = true;
            })
            .ok(),
        "empty read");
  pump(*service);
  check(empty_done, "empty delivered");
  bool abandoned{};
  {
    gneiss::engine::files::read_stream transient(service, "asset://data", 5, 2);
    check(transient.read_next([&](auto) { abandoned = true; }).ok(), "transient read");
  }
  pump(*service);
  check(!abandoned, "destroyed observer not called");
}

void lifetime() {
  auto backend = std::make_shared<controlled_backend>();
  gneiss::engine::files::service service({1, 64});
  gneiss::engine::files::service other;
  check(service.mount({"asset://", {backend}, {}}) == error::success, "delayed mount");
  gneiss::engine::files::request_id id, rejected;
  bool called = false, closed = false;
  check(service.read(
            "asset://a", {16},
            [&](auto completed) {
              check(completed.status == error::cancelled && completed.data.empty(),
                    "cancel result");
              called = true;
              check(service.dispatch_completions(1) == error::invalid_state, "reentrant dispatch");
            },
            id) == error::success,
        "delayed submit");
  check(service.read(
            "asset://b", {16}, [](auto) {}, rejected) == error::not_ready &&
            rejected.value == 0,
        "capacity");
  check(other.cancel(id) == error::invalid_handle, "cross service");
  check(service.cancel(id) == error::success, "cancel pending");
  check(service.shutdown([&](error status) {
    check(status == error::success && called, "close ordering");
    closed = true;
  }) == error::success,
        "shutdown pending");
  pump(service);
  check(!called && !closed, "wait actual cleanup");
  check(service.read(
            "asset://b", {16}, [](auto) {}, rejected) == error::invalid_state,
        "closed submit");
  backend->complete();
  pump(service, 1);
  check(called && !closed, "shutdown respects budget");
  pump(service);
  check(closed, "closed after cancellation");

  auto failure = std::make_shared<controlled_backend>();
  auto fallback = std::make_shared<controlled_backend>();
  failure->immediate = error::io;
  gneiss::engine::files::service errors;
  check(errors.mount({"asset://", {failure, fallback}, {}}) == error::success, "error mount");
  check(errors.read(
            "asset://a", {16},
            [](auto completed) { check(completed.status == error::io, "io preserved"); },
            id) == error::success,
        "error submit");
  pump(errors);
  check(fallback->reads == 0, "no IO fallback");
  failure->immediate = error::unsupported;
  check(errors.read(
            "asset://a", {16},
            [](auto completed) {
              check(completed.status == error::unsupported, "unsupported preserved");
            },
            id) == error::success,
        "unsupported submit");
  pump(errors);
  check(fallback->reads == 0, "no unsupported fallback");

#if GNEISS_TEST_TASK_THREADS && !defined(__EMSCRIPTEN__)
  auto threaded = std::make_shared<controlled_backend>();
  gneiss::engine::files::service threaded_service;
  check(threaded_service.mount({"asset://", {threaded}, {}}) == error::success, "thread mount");
  const auto owner = std::this_thread::get_id();
  bool delivered = false;
  check(threaded_service.read(
            "asset://a", {16},
            [&](auto completed) {
              check(completed.status == error::success && std::this_thread::get_id() == owner,
                    "owner callback");
              delivered = true;
            },
            id) == error::success,
        "thread submit");
  std::thread worker([&] {
    check(threaded_service.dispatch_completions(1) == error::invalid_state, "wrong thread");
    threaded->complete();
  });
  worker.join();
  check(!delivered, "worker only completes");
  pump(threaded_service);
  check(delivered, "owner delivers");
#endif

  gneiss::engine::files::callback<gneiss::engine::files::bytes> late;
  {
    auto abandoned = std::make_shared<controlled_backend>();
    gneiss::engine::files::service dying;
    check(dying.mount({"asset://", {abandoned}, {}}) == error::success, "abandon mount");
    check(dying.read(
              "asset://a", {16}, [](auto) { check(false, "destroyed owner callback"); }, id) ==
              error::success,
          "abandon submit");
    late = std::move(abandoned->completion);
  }
  late({error::success, {}});
}
class controlled_stream final : public gneiss::engine::files::backend_stream {
public:
  gneiss::engine::files::callback<gneiss::engine::files::block> callback;
  std::size_t reads{};
  std::uint64_t offset{};
  bool closed{};
  error read_next(std::size_t maximum,
                  gneiss::engine::files::callback<gneiss::engine::files::block> done) override {
    check(maximum == 8, "stream block bound");
    if (closed)
      return error::invalid_state;
    if (callback)
      return error::not_ready;
    ++reads;
    callback = std::move(done);
    return error::success;
  }
  void complete(std::size_t count, bool eof = false) {
    auto done = std::move(callback);
    const auto begin = offset;
    offset += count;
    done({error::success, {gneiss::engine::files::bytes(count, std::byte{7}), begin, eof}});
  }
  void close() noexcept override {
    closed = true;
    if (auto done = std::move(callback))
      done({error::cancelled, {}});
  }
};
class streaming_backend final : public gneiss::engine::files::backend {
public:
  std::shared_ptr<controlled_stream> stream;
  unsigned opens{};
  bool missing{}, deferred{};
  gneiss::engine::files::callback<std::shared_ptr<gneiss::engine::files::backend_stream>> opening;
  error open_stream(
      std::string_view,
      gneiss::engine::files::callback<std::shared_ptr<gneiss::engine::files::backend_stream>> done)
      override {
    ++opens;
    if (missing)
      return error::not_found;
    stream = std::make_shared<controlled_stream>();
    if (deferred)
      opening = std::move(done);
    else
      done({error::success, stream});
    return error::success;
  }
};
void sequential_streams() {
  auto files = std::make_shared<gneiss::engine::files::service>(
      gneiss::engine::files::service_options{.max_file_bytes = 16, .max_buffer_bytes = 16});
  auto missing = std::make_shared<streaming_backend>();
  missing->missing = true;
  auto backend = std::make_shared<streaming_backend>();
  check(files->mount({"asset://", {missing, backend}, {}}).ok(), "stream mount");
  auto stream = std::make_unique<gneiss::engine::files::read_stream>(files, "asset://unknown",
                                                                     std::nullopt, 8);
  unsigned received = 0;
  check(stream->read_next([&](auto value) {
                check(value.status.ok() && value.data.offset == 0 && value.data.bytes.size() == 3 &&
                          !value.data.eof,
                      "first unknown block");
                ++received;
              }).ok() &&
            received == 0,
        "stream open is deferred");
  for (int i = 0; i < 3; ++i)
    pump(*files);
  check(missing->opens == 1 && backend->opens == 1 && backend->stream->reads == 1, "open fallback");
  check(stream->read_next([](auto) {}) == error::not_ready, "one block in flight");
  gneiss::engine::files::request_id request;
  check(files->read(
            "asset://other", {1}, [](auto) {}, request) == error::not_ready,
        "JS and Wasm block budgets reserved");
  backend->stream->complete(3);
  check(received == 0, "backend completion is deferred");
  pump(*files);
  for (int i = 0; i < 3; ++i)
    pump(*files);
  check(received == 1 && backend->stream->reads == 1, "slow consumer does not prefetch");
  check(stream
            ->read_next([&](auto value) {
              check(value.status.ok() && value.data.offset == 3 && value.data.bytes.empty() &&
                        value.data.eof,
                    "unknown EOF");
              ++received;
            })
            .ok(),
        "EOF request");
  backend->stream->complete(0, true);
  pump(*files);
  check(received == 2 && backend->stream->closed &&
            stream->read_next([](auto) {}) == error::invalid_state,
        "EOF closes source");
  stream = std::make_unique<gneiss::engine::files::read_stream>(files, "asset://cancel",
                                                                std::nullopt, 8);
  error cancelled;
  check(stream->read_next([&](auto value) { cancelled = value.status; }).ok(),
        "cancel stream open");
  for (int i = 0; i < 3; ++i)
    pump(*files);
  check(stream->cancel().ok() && backend->stream->closed && cancelled == error::unknown,
        "cancel aborts source");
  pump(*files);
  check(cancelled == error::cancelled, "cancel delivered once");
  stream = std::make_unique<gneiss::engine::files::read_stream>(files, "asset://invalid",
                                                                std::nullopt, 8);
  error oversized;
  check(stream->read_next([&](auto value) { oversized = value.status; }).ok(),
        "invalid stream open");
  for (int i = 0; i < 3; ++i)
    pump(*files);
  backend->stream->complete(9);
  pump(*files);
  check(oversized == error::io && backend->stream->closed, "backend block violation closes source");
  stream = std::make_unique<gneiss::engine::files::read_stream>(files, "asset://destroy",
                                                                std::nullopt, 8);
  check(
      stream->read_next([](auto) { check(false, "destroyed consumer must not be notified"); }).ok(),
      "destruction stream open");
  for (int i = 0; i < 3; ++i)
    pump(*files);
  auto abandoned = backend->stream;
  stream.reset();
  check(abandoned->closed, "destruction aborts pending read");
  pump(*files);
  stream = std::make_unique<gneiss::engine::files::read_stream>(files, "asset://unopened",
                                                                std::nullopt, 8);
  check(stream->read_next([](auto) { check(false, "destroyed opening consumer"); }).ok(),
        "late open");
  stream.reset();
  for (int i = 0; i < 3; ++i)
    pump(*files);
  check(backend->stream->closed, "late open closes source");
  backend->deferred = true;
  stream = std::make_unique<gneiss::engine::files::read_stream>(files, "asset://late-cancel",
                                                                std::nullopt, 8);
  error late_status;
  check(stream->read_next([&](auto value) { late_status = value.status; }).ok(), "deferred open");
  pump(*files);
  check(stream->cancel().ok(), "cancel pending open");
  auto late_source = backend->stream;
  auto opened = std::move(backend->opening);
  opened({error::success, late_source});
  pump(*files);
  check(late_status == error::cancelled && late_source->closed && late_source->reads == 0,
        "cancelled opening closes retained source without reading");
  backend->deferred = false;
  stream =
      std::make_unique<gneiss::engine::files::read_stream>(files, "asset://close", std::nullopt, 8);
  check(stream
            ->read_next([](auto value) {
              check(value.status == error::cancelled, "shutdown cancels stream");
            })
            .ok(),
        "close stream open");
  for (int i = 0; i < 3; ++i)
    pump(*files);
  bool closed = false;
  check(files
            ->shutdown([&](auto value) {
              check(value.ok(), "close result");
              closed = true;
            })
            .ok(),
        "stream service shutdown");
  check(backend->stream->closed && !closed, "shutdown closes backend immediately");
  pump(*files);
  check(closed, "shutdown drained");
}

// 来源标记属于挂载；共享后端不意味着共享资产命名域。
void source_identity() {
  auto backend = std::make_shared<gneiss::engine::files::memory_backend>();
  auto first = std::make_unique<gneiss::engine::files::service>();
  auto second = std::make_unique<gneiss::engine::files::service>();
  std::shared_ptr<const gneiss::engine::files::source_domain> original, independent, nested;
  check(first->resolve_source("asset://item", original) == error::not_found && !original,
        "unmounted identity");
  check(first->mount({"asset://", {backend}, {}}).ok(), "first identity mount");
  check(second->mount({"asset://", {backend}, {}}).ok(), "independent identity mount");
  check(first->resolve_source("asset://item", original).ok() && original, "identity without IO");
  check(second->resolve_source("asset://item", independent).ok() && independent != original,
        "same backend separate domains");
  check(second->mount({"asset://nested/", {backend}, {}, original}).ok(), "explicit shared domain");
  check(second->resolve_source("asset://nested/item", nested).ok() && nested == original,
        "longest prefix source");
  check(first->resolve_source("asset://../invalid", nested) == error::invalid_argument && !nested,
        "invalid identity clears output");
  std::weak_ptr<const gneiss::engine::files::source_domain> lifetime = original;
  first.reset();
  second.reset();
  check(!lifetime.expired(), "asset identity retains domain");
  original.reset();
  check(lifetime.expired(), "last identity releases domain");
}
} // namespace
int main() {
  try {
    sequential_streams();
    source_identity();
    memory_and_routes();
    pagination_and_reentry();
    byte_budget();
    stream_blocks();
    lifetime();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
