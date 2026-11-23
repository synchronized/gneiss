// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "gneiss/engine/function/files/backends/native.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <gneiss/engine/function/files/service.hpp>
#include <gneiss/engine/platform/io/executor.hpp>
#include <stdexcept>
#include <thread>
using namespace gneiss;
void check(bool value) {
  if (!value)
    throw std::runtime_error("文件后端契约失败");
}
int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  io::executor executor;
  check(executor.start().ok());
  auto backend = gneiss::files::create_backend_native({executor, argv[1]});
  gneiss::files::service service;
  check(service.mount({"test://", {backend}, backend}).ok());
  {
    error initialized = error::not_ready;
    if (service.initialize([&](error status) { initialized = status; }).failed() ||
        service.dispatch_completions(64).failed() || initialized.failed())
      throw std::runtime_error("文件服务初始化失败");
  }
  gneiss::files::request_id request;
  bool done{};
  error status;
  const auto pump = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
      check(service.dispatch_completions(64).ok());
      std::this_thread::yield();
    }
    check(done);
    done = false;
  };
  const auto complete = [&](error value) {
    status = value;
    done = true;
  };
  check(backend->write("", {}, {}, complete) == error::invalid_argument);
  check(backend->write("directory/", {}, {}, complete) == error::invalid_argument);
  gneiss::files::bytes bytes{std::byte{1}, std::byte{2}, std::byte{3}};
  check(service.write("test://native.bin", bytes, {}, complete, request).ok());
  check(!done);
  pump();
  check(status.ok());
  gneiss::files::bytes output;
  const auto read = [&](gneiss::files::completion<gneiss::files::bytes> value) {
    status = value.status;
    output = std::move(value.data);
    done = true;
  };
  check(service.read("test://native.bin", {16}, read, request).ok());
  pump();
  check(status.ok() && output == bytes);
  check(service.read_range("test://native.bin", {1, 2}, read, request).ok());
  pump();
  check(status.ok() && output == gneiss::files::bytes({std::byte{2}, std::byte{3}}));
  check(service.read_range("test://native.bin", {2, 2}, read, request).ok());
  pump();
  check(status == error::invalid_argument && output.empty());
  check(service.read("test://native.bin", {2}, read, request).ok());
  pump();
  check(status == error::invalid_argument);
  check(service.read("test://missing.bin", {16}, read, request).ok());
  pump();
  check(status == error::not_found);
  check(service
            .write("test://native.bin", {std::byte{9}}, {gneiss::files::write_mode::create_new},
                   complete, request)
            .ok());
  pump();
  check(status == error::invalid_state);
  check(service.read("test://native.bin", {16}, read, request).ok());
  pump();
  check(status.ok() && output == bytes);
  // 同一路径替换只在完整写入后可见。
  std::reverse(bytes.begin(), bytes.end());
  check(service.write("test://native.bin", bytes, {}, complete, request).ok());
  pump();
  check(status.ok());
  check(service.read("test://native.bin", {16}, read, request).ok());
  pump();
  check(status.ok() && output == bytes);
  gneiss::files::info info;
  check(service
            .stat(
                "test://native.bin",
                [&](gneiss::files::completion<gneiss::files::info> value) {
                  status = value.status;
                  info = value.data;
                  done = true;
                },
                request)
            .ok());
  pump();
  check(status.ok() && info.size == 3);
  gneiss::files::directory_page page;
  check(service
            .list(
                "test://", {1, {}},
                [&](gneiss::files::completion<gneiss::files::directory_page> value) {
                  status = value.status;
                  page = std::move(value.data);
                  done = true;
                },
                request)
            .ok());
  pump();
  check(status.ok() && page.entries.size() == 1);
  check(service.flush("test://", complete, request).ok());
  pump();
  check(status.ok());
  check(service.shutdown(complete).ok());
  pump();
  check(status.ok());
  check(executor.stop().ok());
  const auto path = std::string(argv[1]) + "/native.bin";
  check(std::remove(path.c_str()) == 0);
}
