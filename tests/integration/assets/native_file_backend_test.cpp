// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/platform/files/file_service.hpp"
#include "engine/platform/files/native_file_backend.hpp"
#include "engine/platform/io/uv/uv_loop_executor.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
using namespace gneiss::engine;
void check(bool value) {
  if (!value)
    throw std::runtime_error("文件后端契约失败");
}
int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  uv_loop_executor executor;
  check(executor.start().ok());
  auto backend = std::make_shared<native_file_backend>(executor, argv[1]);
  file_service service;
  check(service.mount({"test://", {backend}, backend}).ok());
  file_request_id request;
  bool done{};
  result status;
  const auto pump = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
      check(service.dispatch_completions(64).ok());
      std::this_thread::yield();
    }
    check(done);
    done = false;
  };
  const auto complete = [&](result value) {
    status = value;
    done = true;
  };
  check(backend->write("", {}, {}, complete) == result::invalid_argument);
  check(backend->write("directory/", {}, {}, complete) == result::invalid_argument);
  file_bytes bytes{std::byte{1}, std::byte{2}, std::byte{3}};
  check(service.write("test://native.bin", bytes, {}, complete, request).ok());
  check(!done);
  pump();
  check(status.ok());
  file_bytes output;
  const auto read = [&](file_completion<file_bytes> value) {
    status = value.status;
    output = std::move(value.data);
    done = true;
  };
  check(service.read("test://native.bin", {16}, read, request).ok());
  pump();
  check(status.ok() && output == bytes);
  check(service.read_range("test://native.bin", {1, 2}, read, request).ok());
  pump();
  check(status.ok() && output == file_bytes({std::byte{2}, std::byte{3}}));
  check(service.read_range("test://native.bin", {2, 2}, read, request).ok());
  pump();
  check(status == result::invalid_argument && output.empty());
  check(service.read("test://native.bin", {2}, read, request).ok());
  pump();
  check(status == result::invalid_argument);
  check(service.read("test://missing.bin", {16}, read, request).ok());
  pump();
  check(status == result::not_found);
  check(service
            .write("test://native.bin", {std::byte{9}}, {write_mode::create_new}, complete, request)
            .ok());
  pump();
  check(status == result::invalid_state);
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
  file_info info;
  check(service
            .stat(
                "test://native.bin",
                [&](file_completion<file_info> value) {
                  status = value.status;
                  info = value.data;
                  done = true;
                },
                request)
            .ok());
  pump();
  check(status.ok() && info.size == 3);
  directory_page page;
  check(service
            .list(
                "test://", {1, {}},
                [&](file_completion<directory_page> value) {
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
