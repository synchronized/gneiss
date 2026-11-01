// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/platform/files/browser/browser_file_backend.hpp"
#include "engine/platform/files/browser/fetch_file_backend.hpp"
#include "engine/platform/files/file_read_stream.hpp"
#include <cstdlib>
#include <emscripten.h>
#include <memory>
using namespace gneiss::engine;
namespace {
std::shared_ptr<gneiss::engine::files::browser_backend> local;
std::shared_ptr<gneiss::engine::files::fetch_backend> remote;
const gneiss::engine::files::bytes bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                         std::byte{5}};
void check(bool condition) {
  if (!condition) {
    EM_ASM({ document.body.textContent = 'FAIL'; });
    std::abort();
  }
}
void stream_checks();
void unsupported_stream();
void fetch_checks() {
  remote = std::make_shared<gneiss::engine::files::fetch_backend>(
      emscripten_run_script_string("location.origin + '/'"));
  check(
      remote
          ->read_range(
              "bytes", {1, 3},
              [](auto value) {
                check(value.status.ok() && value.data == gneiss::engine::files::bytes(
                                                             bytes.begin() + 1, bytes.begin() + 4));
                check(
                    remote
                        ->read_range(
                            "ignore", {1, 3},
                            [](auto ignored) {
                              check(ignored.status == error::unsupported);
                              check(
                                  remote
                                      ->read_range(
                                          "bad", {1, 3},
                                          [](auto bad) {
                                            check(bad.status == error::io);
                                            check(
                                                remote
                                                    ->read(
                                                        "missing", {8},
                                                        [](auto missing) {
                                                          check(missing.status == error::not_found);
                                                          check(
                                                              remote
                                                                  ->read(
                                                                      "bytes", {2},
                                                                      [](auto large) {
                                                                        check(large.status ==
                                                                              error::
                                                                                  invalid_argument);
                                                                        check(
                                                                            remote
                                                                                ->stat(
                                                                                    "bytes",
                                                                                    [](auto info) {
                                                                                      check(
                                                                                          info.status
                                                                                              .ok() &&
                                                                                          info.data
                                                                                                  .size ==
                                                                                              5);
                                                                                      unsupported_stream();
                                                                                    })
                                                                                .ok());
                                                                      })
                                                                  .ok());
                                                        })
                                                    .ok());
                                          })
                                      .ok());
                            })
                        .ok());
              })
          .ok());
}
std::shared_ptr<gneiss::engine::files::service> streaming_files;
std::unique_ptr<gneiss::engine::files::read_stream> streaming;
unsigned phase{}, pause_frames{}, delivered{}, paused_reads{};
std::uint64_t streamed_bytes{};
bool waiting{}, finished{};
void stream_tick() {
  check(streaming_files->dispatch_completions(16).ok());
  if (finished) {
    streaming.reset();
    streaming_files.reset();
    emscripten_cancel_main_loop();
    check(EM_ASM_INT({ return GneissFetchStreams.entries.size; }) == 0);
    EM_ASM({ document.body.textContent = 'PASS'; });
    return;
  }
  if (waiting)
    return;
  if (phase == 0) {
    if (pause_frames) {
      check(EM_ASM_INT({ return globalThis.gneissStreamReads; }) == static_cast<int>(paused_reads));
      --pause_frames;
      return;
    }
    waiting = true;
    check(streaming
              ->read_next([](auto value) {
                check(value.status.ok() && value.data.offset == streamed_bytes &&
                      value.data.bytes.size() <= 32);
                for (std::size_t i = 0; i < value.data.bytes.size(); ++i)
                  check(value.data.bytes[i] == static_cast<std::byte>((streamed_bytes + i) % 251));
                streamed_bytes += value.data.bytes.size();
                ++delivered;
                waiting = false;
                if (value.data.eof) {
                  check(streamed_bytes == 257 && delivered > 8);
                  phase = 10;
                } else {
                  pause_frames = 3;
                  paused_reads = EM_ASM_INT({ return globalThis.gneissStreamReads; });
                }
              })
              .ok());
    check(streaming->read_next([](auto) { check(false); }) == error::not_ready);
  } else if (phase == 10) {
    streaming = std::make_unique<gneiss::engine::files::read_stream>(
        streaming_files, "remote://empty", std::nullopt, 32);
    waiting = true;
    check(streaming
              ->read_next([](auto value) {
                check(value.status.ok() && value.data.eof && value.data.bytes.empty() &&
                      value.data.offset == 0);
                waiting = false;
                phase = 1;
              })
              .ok());
  } else if (phase == 1) {
    streaming = std::make_unique<gneiss::engine::files::read_stream>(
        streaming_files, "remote://stall", std::nullopt, 32);
    waiting = true;
    check(streaming
              ->read_next([](auto value) {
                check(value.status == error::cancelled);
                waiting = false;
                phase = 3;
              })
              .ok());
    phase = 2;
    // 等待响应头和在途 read()；取消必须立即终止来源，不等服务端结束。
    emscripten_async_call([](void*) { check(streaming->cancel().ok()); }, nullptr, 100);
  } else if (phase == 3) {
    streaming = std::make_unique<gneiss::engine::files::read_stream>(
        streaming_files, "remote://stream", std::nullopt, 32);
    waiting = true;
    check(streaming
              ->read_next([](auto value) {
                check(value.status.ok() && !value.data.eof);
                waiting = false;
                phase = 4;
              })
              .ok());
  } else if (phase == 4) {
    // 消费者暂停时服务关闭也必须释放 reader。
    waiting = true;
    check(streaming_files
              ->shutdown([](error status) {
                check(status.ok());
                finished = true;
              })
              .ok());
  }
}
void unsupported_stream() {
  // clang-format off
  EM_ASM({
    const original = ReadableStream.prototype.getReader;
    ReadableStream.prototype.getReader = function(options) {
      if (options && options.mode == 'byob') {
        ReadableStream.prototype.getReader = original;
        throw new TypeError('injected unsupported BYOB');
      }
      return original.call(this, options);
    };
  });
  // clang-format on
  check(remote
            ->open_stream("stream",
                          [](auto opened) {
                            check(opened.status == error::unsupported && !opened.data);
                            check(EM_ASM_INT({ return GneissFetchStreams.entries.size; }) == 0);
                            stream_checks();
                          })
            .ok());
}
void stream_checks() {
  EM_ASM({
    globalThis.gneissStreamReads = 0;
    const original = ReadableStreamBYOBReader.prototype.read;
    ReadableStreamBYOBReader.prototype.read = function(... args) {
      ++globalThis.gneissStreamReads;
      return original.apply(this, args);
    };
  });
  streaming_files = std::make_shared<gneiss::engine::files::service>(
      gneiss::engine::files::service_options{.max_file_bytes = 32, .max_buffer_bytes = 64});
  const auto base = std::string(emscripten_run_script_string("location.origin + '/'"));
  auto absent = std::make_shared<gneiss::engine::files::fetch_backend>(base + "absent/");
  check(streaming_files->mount({"remote://", {absent, remote}, {}}).ok());
  streaming = std::make_unique<gneiss::engine::files::read_stream>(
      streaming_files, "remote://stream", std::nullopt, 32);
  emscripten_set_main_loop(stream_tick, 0, false);
}
void restore() {
  local.reset();
  local = std::make_shared<gneiss::engine::files::browser_backend>(
      "/gneiss-files-test", gneiss::engine::files::browser_storage::persistent);
  check(local
            ->initialize([](error status) {
              check(status.ok());
              check(local
                        ->read("data", {8},
                               [](auto value) { check(value.status.ok() && value.data == bytes); })
                        .ok());
              check(local
                        ->read_range(
                            "data", {5, 0},
                            [](auto value) { check(value.status.ok() && value.data.empty()); })
                        .ok());
              // 临时文件短写失败不能截断旧目标，也不能遗留临时条目。
              EM_ASM({
                const original = FS.write;
                FS.write = (stream, buffer, offset, length, position, canOwn) => {
                  FS.write = original;
                  return original(stream, buffer, offset, Math.min(length, 1), position, canOwn);
                };
              });
              check(local->write("data", bytes, {}, [](auto) { check(false); }) == error::io);
              check(local
                        ->read("data", {8},
                               [](auto value) { check(value.status.ok() && value.data == bytes); })
                        .ok());
              check(local->read_range("data", {4, 2}, [](auto) {}) == error::invalid_argument);
              check(local->read("missing", {8}, [](auto) {}) == error::not_found);
              check(local->write("data", {}, {gneiss::engine::files::write_mode::create_new},
                                 [](auto) {}) == error::invalid_state);
              check(local
                        ->stat("data",
                               [](auto value) { check(value.status.ok() && value.data.size == 5); })
                        .ok());
              check(local
                        ->list("", {},
                               [](auto value) {
                                 check(value.status.ok() && value.data.entries.size() == 1 &&
                                       value.data.entries.front().name == "data");
                               })
                        .ok());
              fetch_checks();
            })
            .ok());
}
}
int main() {
  EM_ASM({
    FS.mkdir('/preloaded');
    FS.mkdir('/preloaded/assets');
    FS.writeFile('/preloaded/assets/data', new Uint8Array([ 1, 2, 3, 4, 5 ]));
    FS.symlink('/preloaded/assets', '/preloaded/link');
  });
  {
    gneiss::engine::files::browser_backend borrowed(
        "/preloaded/assets", gneiss::engine::files::browser_storage::existing);
    check(borrowed.initialize([](error status) { check(status.ok()); }).ok());
    check(
        borrowed
            .read("data", {8}, [](auto value) { check(value.status.ok() && value.data == bytes); })
            .ok());
    check(borrowed.initialize([](error) {}) == error::invalid_state);
    gneiss::engine::files::browser_backend missing(
        "/absent", gneiss::engine::files::browser_storage::existing);
    check(missing.initialize([](error) { check(false); }) == error::not_found);
    gneiss::engine::files::browser_backend linked("/preloaded/link",
                                                  gneiss::engine::files::browser_storage::existing);
    check(linked.initialize([](error) { check(false); }) == error::invalid_argument);
  }
  check(EM_ASM_INT({ return FS.readFile('/preloaded/assets/data').length; }) == 5);

  local = std::make_shared<gneiss::engine::files::browser_backend>(
      "/gneiss-files-test", gneiss::engine::files::browser_storage::persistent);
  check(local->read("data", {8}, [](auto) {}) == error::not_ready);
  check(local
            ->initialize([](error status) {
              check(status.ok());
              check(local
                        ->write(
                            "data", bytes, {},
                            [](error written) {
                              check(written.ok());
                              // 注入一次持久化失败，验证内存数据保留及 flush 可重试。
                              EM_ASM({
                                const mount =
                                    FS.lookupPath('/gneiss-files-test', {follow_mount : true})
                                        .node.mount;
                                const original = mount.type.syncfs;
                                mount.type.syncfs = (target, populate, callback) => {
                                  mount.type.syncfs = original;
                                  callback(new Error('injected persistence failure'));
                                };
                              });
                              bool failed = false;
                              check(local
                                        ->flush([&](error status) {
                                          check(status == error::io);
                                          failed = true;
                                        })
                                        .ok() &&
                                    failed);
                              check(local
                                        ->read("data", {8},
                                               [](auto value) {
                                                 check(value.status.ok() && value.data == bytes);
                                               })
                                        .ok());
                              check(local
                                        ->flush([](error flushed) {
                                          check(flushed.ok());
                                          // 等本轮回调释放保活引用后再重新挂载。
                                          emscripten_async_call([](void*) { restore(); }, nullptr,
                                                                0);
                                        })
                                        .ok());
                              check(local->write("late", {}, {}, [](auto) {}) == error::not_ready);
                              check(local->flush([](auto) {}) == error::not_ready);
                            })
                        .ok());
            })
            .ok());
  return 0;
}
