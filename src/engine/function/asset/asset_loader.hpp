// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/diagnostic.hpp"
#include "engine/function/asset/asset_loading.hpp"
#include "engine/resource/asset_diagnostic.hpp"
#include <memory>
#include <string>
#include <vector>
namespace gneiss::engine {
/** 一次加载的格式状态；管理器负责 I/O 和调度，Loader 只消费字节并描述下一次读取。 */
struct asset_loader {
  std::string uri, next_uri;
  std::uint32_t source_type{};
  std::shared_ptr<const void> value;
  asset_load_metrics metrics;
  serialization::diagnostic diagnostic;
  error status{error::not_ready};
  explicit asset_loader(std::string path) : uri(std::move(path)), next_uri(uri) {}
  virtual ~asset_loader() = default;
  virtual void decode(const std::vector<std::byte>& bytes) = 0;
  std::size_t input_bytes{};
  void reject(asset_diagnostic failure) {
    status = failure.error;
    diagnostic = {failure.byte_offset, std::move(failure.path), std::move(failure.message), status};
  }
};

}
