// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/platform/files/browser/browser_file_backend.hpp"
#include "engine/platform/files/browser/fetch_file_backend.hpp"
#include <cstdlib>
#include <emscripten.h>
#include <memory>
using namespace gneiss::engine;
namespace {
std::shared_ptr<browser_file_backend> local;
std::shared_ptr<fetch_file_backend> remote;
const file_bytes bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
void check(bool condition) {
  if (!condition) {
    EM_ASM({ document.body.textContent = 'FAIL'; });
    std::abort();
  }
}
void fetch_checks() {
  remote =
      std::make_shared<fetch_file_backend>(emscripten_run_script_string("location.origin + '/'"));
  check(
      remote
          ->read_range(
              "bytes", {1, 3},
              [](auto value) {
                check(value.status.ok() &&
                      value.data == file_bytes(bytes.begin() + 1, bytes.begin() + 4));
                check(
                    remote
                        ->read_range(
                            "ignore", {1, 3},
                            [](auto ignored) {
                              check(ignored.status == result::unsupported);
                              check(
                                  remote
                                      ->read_range(
                                          "bad", {1, 3},
                                          [](auto bad) {
                                            check(bad.status == result::io);
                                            check(
                                                remote
                                                    ->read(
                                                        "missing", {8},
                                                        [](auto missing) {
                                                          check(missing.status ==
                                                                result::not_found);
                                                          check(
                                                              remote
                                                                  ->read(
                                                                      "bytes", {2},
                                                                      [](auto large) {
                                                                        check(large.status ==
                                                                              result::
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
                                                                                      EM_ASM({
                                                                                        document
                                                                                            .body
                                                                                            .textContent =
                                                                                            'PASS';
                                                                                      });
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
void restore() {
  local.reset();
  local = std::make_shared<browser_file_backend>("/gneiss-files-test",
                                                 browser_file_storage::persistent);
  check(local
            ->initialize([](result status) {
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
              check(local->read_range("data", {4, 2}, [](auto) {}) == result::invalid_argument);
              check(local->read("missing", {8}, [](auto) {}) == result::not_found);
              check(local->write("data", {}, {write_mode::create_new}, [](auto) {}) ==
                    result::invalid_state);
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
    browser_file_backend borrowed("/preloaded/assets", browser_file_storage::existing);
    check(borrowed.initialize([](result status) { check(status.ok()); }).ok());
    check(
        borrowed
            .read("data", {8}, [](auto value) { check(value.status.ok() && value.data == bytes); })
            .ok());
    check(borrowed.initialize([](result) {}) == result::invalid_state);
    browser_file_backend missing("/absent", browser_file_storage::existing);
    check(missing.initialize([](result) { check(false); }) == result::not_found);
    browser_file_backend linked("/preloaded/link", browser_file_storage::existing);
    check(linked.initialize([](result) { check(false); }) == result::invalid_argument);
  }
  check(EM_ASM_INT({ return FS.readFile('/preloaded/assets/data').length; }) == 5);

  local = std::make_shared<browser_file_backend>("/gneiss-files-test",
                                                 browser_file_storage::persistent);
  check(local->read("data", {8}, [](auto) {}) == result::not_ready);
  check(local
            ->initialize([](result status) {
              check(status.ok());
              check(local
                        ->write(
                            "data", bytes, {},
                            [](result written) {
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
                                        ->flush([&](result status) {
                                          check(status == result::io);
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
                                        ->flush([](result flushed) {
                                          check(flushed.ok());
                                          // 等本轮回调释放保活引用后再重新挂载。
                                          emscripten_async_call([](void*) { restore(); }, nullptr,
                                                                0);
                                        })
                                        .ok());
                              check(local->write("late", {}, {}, [](auto) {}) == result::not_ready);
                              check(local->flush([](auto) {}) == result::not_ready);
                            })
                        .ok());
            })
            .ok());
  return 0;
}
