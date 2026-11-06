// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/type_id.hpp"
#include "engine/function/asset/asset_loading.hpp"
#include "engine/resource/asset_ref.hpp"
#include <memory>
#include <optional>
#include <tuple>
namespace gneiss::engine::detail {
struct load_operation;
using asset_key =
    std::tuple<std::shared_ptr<const files::source_domain>, std::string, type_id, std::string>;
struct asset_cache_value final : std::enable_shared_from_this<asset_cache_value> {
  std::string uri;
  std::shared_ptr<const files::source_domain> source;
  type_id type{engine::runtime_type_of<void>()};
  bool is_loading{true}; // 仅在同步创建回调期间阻止同键递归。
  std::shared_ptr<const void> resource;
  mutable asset_load_metrics metrics;
  mutable std::size_t source_bytes{}; // CPU 文件加载统计；程序创建的对象默认为零。
  /** 类型化引用保活条目与资源，保证只持有类型化引用时也能命中弱索引。 */
  template <typename T> [[nodiscard]] asset_ref<const T> as() const noexcept {
    auto owner = weak_from_this().lock();
    if (type != runtime_type_of<T>() || !owner)
      return {};
    return asset_ref<const T>{
        std::shared_ptr<const T>(std::move(owner), static_cast<const T*>(resource.get()))};
  }
};

/** 迁移期统一身份记录；cached 仍包装尚未迁移的 CPU 描述。 */
struct asset_entry {
  asset_key identity;
  std::optional<std::weak_ptr<const asset_cache_value>> cached;
  std::shared_ptr<load_operation> loading;
};
}
