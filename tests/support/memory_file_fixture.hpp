// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_backend.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_identity.hpp"
#include "engine/resource/asset_uri.hpp"
#include <limits>
namespace gneiss::test_support {
// 可变内存测试数据：子类提供内存拷贝，不允许磁盘或网络读取。
class memory_file_fixture : public gneiss::files::backend {
public:
  virtual gneiss::error read(std::string_view path,
                             gneiss::files::bytes& bytes) const noexcept = 0;
  virtual gneiss::error read_bounded(std::string_view path, std::size_t limit,
                                     gneiss::files::bytes& bytes) const noexcept {
    gneiss::files::bytes data;
    auto status = read(path, data);
    if (status.failed())
      return status;
    if (data.size() > limit)
      return gneiss::error::invalid_argument;
    bytes = std::move(data);
    return gneiss::error::success;
  }
  gneiss::error read(std::string_view path, gneiss::files::read_options options,
                     gneiss::files::callback<gneiss::files::bytes> callback) final {
    pending_.push_back({std::string(path), options, std::move(callback)});
    return gneiss::error::success;
  }
  void poll() noexcept final {
    auto pending = std::move(pending_);
    pending_.clear();
    for (auto& request : pending) {
      gneiss::files::bytes data;
      const auto status = read_bounded(request.path, request.options.max_bytes, data);
      request.callback({status, std::move(data)});
    }
  }

private:
  struct request {
    std::string path;
    gneiss::files::read_options options;
    gneiss::files::callback<gneiss::files::bytes> callback;
  };
  std::vector<request> pending_;
};
// 单挂载的测试装配；真正的路由、来源身份与完成分发全部由 file_service 实现。
struct fixture_files {
  std::shared_ptr<gneiss::files::service> service =
      std::make_shared<gneiss::files::service>(
          gneiss::files::service_options{.max_file_bytes = 64U * 1024U * 1024U});
  std::shared_ptr<memory_file_fixture> memory;
  std::string prefix;
  gneiss::error mount(std::string_view point, std::shared_ptr<memory_file_fixture> backend) {
    if (memory)
      return gneiss::error::invalid_state;
    auto status = service->mount({std::string(point), {backend}, {}});
    if (status.ok()) {
      prefix = point;
      memory = std::move(backend);
    }
    return status;
  }
  gneiss::error read_bounded(std::string_view uri, std::size_t limit,
                             gneiss::files::bytes& bytes) const {
    if (gneiss::validate_uri(uri).failed())
      return gneiss::error::invalid_argument;
    if (!memory || !uri.starts_with(prefix))
      return gneiss::error::not_found;
    return memory->read_bounded(uri.substr(prefix.size()), limit, bytes);
  }
  gneiss::error identify(std::string_view uri, std::uint32_t type,
                         gneiss::asset_identity& output) const {
    output = {};
    if (!type || gneiss::validate_uri(uri).failed())
      return gneiss::error::invalid_argument;
    std::shared_ptr<const gneiss::files::source_domain> source;
    auto status = service->resolve_source(uri, source);
    if (status.ok())
      output = {std::move(source), std::string(uri), type};
    return status;
  }
};
inline std::shared_ptr<gneiss::files::service> test_files(const fixture_files& files) {
  return files.service;
}
inline std::shared_ptr<gneiss::files::service>
test_files(std::shared_ptr<gneiss::files::service> files) {
  return files;
}
}
