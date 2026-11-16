// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/serialization/diagnostic.hpp>
#include <gneiss/engine/core/types/type_id.hpp>
#include <gneiss/engine/function/asset/asset_loading.hpp>
#include <gneiss/engine/resource/asset.hpp>
#include <gneiss/engine/resource/asset_diagnostic.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace gneiss {
class asset_manager;
/** 一次加载声明的必要依赖；调度器推进，Loader 仅在装配时读取结果。 */
struct asset_dependency {
  type_id type;
  std::string uri, path;
  asset_load_options options;
  asset_dependency(type_id type, std::string uri, asset_load_options options, std::string path = {})
      : type(type), uri(std::move(uri)), path(std::move(path)), options(options) {}
  virtual ~asset_dependency() = default;
  virtual error advance(asset_manager&) = 0;
  /** 异步依赖状态变化时投递父操作，不能直接运行父操作。 */
  std::function<void()> notify;
  asset_diagnostic diagnostic;
};
/** 一次加载的格式状态；管理器负责 I/O 和调度，Loader 消费字节并声明后续读取与必要依赖。 */
struct asset_loader {
  std::string uri, next_uri;
  std::uint32_t source_type{};
  std::shared_ptr<const void> value;
  asset_load_metrics metrics;
  serialization::diagnostic diagnostic;
  error status{error::not_ready};
  explicit asset_loader(std::string path) : uri(std::move(path)), next_uri(uri) {}
  virtual ~asset_loader() = default;
  /** 所属线程创建未就绪身份；通用自定义 Loader 可选择不支持提前引用。 */
  virtual std::shared_ptr<const asset> reserve() { return {}; }
  virtual void decode(const std::vector<std::byte>& bytes) = 0;
  std::vector<std::shared_ptr<asset_dependency>> dependencies;
  /** 全部声明依赖就绪后装配；不提交请求或推进服务。 */
  virtual error finish() { return error::success; }
  /** 所属线程采用成功结果；只有仍持有记录更新权的操作才会调用。 */
  virtual void adopt(const std::shared_ptr<const void>&) {}
  std::size_t input_bytes{};
  void reject(asset_diagnostic failure) {
    status = failure.error;
    diagnostic = {failure.byte_offset, std::move(failure.path), std::move(failure.message), status};
  }
};

}
