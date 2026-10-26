// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_backend.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_uri.hpp"
#include "engine/resource/types/asset_identity.hpp"
#include <limits>
namespace gneiss::test_support {
// 可变内存测试数据：子类提供内存拷贝，不允许磁盘或网络读取。
class memory_file_fixture : public engine::file_backend {
public:
  virtual engine::result read(std::string_view path, engine::file_bytes& bytes) const noexcept = 0;
  virtual engine::result read_bounded(std::string_view path, std::size_t limit,
                                      engine::file_bytes& bytes) const noexcept {
    engine::file_bytes data;
    auto status = read(path, data);
    if (status.failed())
      return status;
    if (data.size() > limit)
      return engine::result::invalid_argument;
    bytes = std::move(data);
    return engine::result::success;
  }
  engine::result read(std::string_view path, engine::read_options options,
                      engine::file_callback<engine::file_bytes> callback) final {
    pending_.push_back({std::string(path), options, std::move(callback)});
    return engine::result::success;
  }
  void poll() noexcept final {
    auto pending = std::move(pending_);
    pending_.clear();
    for (auto& request : pending) {
      engine::file_bytes data;
      const auto status = read_bounded(request.path, request.options.max_bytes, data);
      request.callback({status, std::move(data)});
    }
  }

private:
  struct request {
    std::string path;
    engine::read_options options;
    engine::file_callback<engine::file_bytes> callback;
  };
  std::vector<request> pending_;
};
// 单挂载的测试装配；真正的路由、来源身份与完成分发全部由 file_service 实现。
struct fixture_files {
  std::shared_ptr<engine::file_service> service = std::make_shared<engine::file_service>(
      engine::file_service_options{.max_file_bytes = std::numeric_limits<std::size_t>::max()});
  std::shared_ptr<memory_file_fixture> memory;
  std::string prefix;
  engine::result mount(std::string_view point, std::shared_ptr<memory_file_fixture> backend) {
    if (memory)
      return engine::result::invalid_state;
    auto status = service->mount({std::string(point), {backend}, {}});
    if (status.ok()) {
      prefix = point;
      memory = std::move(backend);
    }
    return status;
  }
  engine::result read_bounded(std::string_view uri, std::size_t limit,
                              engine::file_bytes& bytes) const {
    if (engine::validate_uri(uri).failed())
      return engine::result::invalid_argument;
    if (!memory || !uri.starts_with(prefix))
      return engine::result::not_found;
    return memory->read_bounded(uri.substr(prefix.size()), limit, bytes);
  }
  engine::result identify(std::string_view uri, std::uint32_t type,
                          engine::asset_identity& output) const {
    output = {};
    if (!type || engine::validate_uri(uri).failed())
      return engine::result::invalid_argument;
    std::shared_ptr<const engine::file_source_domain> source;
    auto status = service->resolve_source(uri, source);
    if (status.ok())
      output = {std::move(source), std::string(uri), type};
    return status;
  }
};
inline std::shared_ptr<engine::file_service> test_files(const fixture_files& files) {
  return files.service;
}
inline std::shared_ptr<engine::file_service>
test_files(std::shared_ptr<engine::file_service> files) {
  return files;
}
}
