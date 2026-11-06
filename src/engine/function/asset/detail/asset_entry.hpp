// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/type_id.hpp"
#include "engine/function/asset/asset_loading.hpp"
#include "engine/resource/asset.hpp"
#include <memory>
#include <optional>
#include <tuple>
namespace gneiss::engine::detail {
struct load_operation;
using asset_key =
    std::tuple<std::shared_ptr<const files::source_domain>, std::string, type_id, std::string>;
/** 一次查询取得的强引用与统计副本；不创建控制块，不承担资产身份。 */
struct cached_asset {
  type_id type{runtime_type_of<void>()};
  std::shared_ptr<const asset> resource;
  asset_load_metrics metrics;
  std::size_t source_bytes{};
  template <class T> [[nodiscard]] ref<const T> as() const noexcept {
    return type == runtime_type_of<T>() ? ref<const T>{std::static_pointer_cast<const T>(resource)}
                                        : ref<const T>{};
  }
};
/** 身份、弱缓存及在途操作共用一条记录；只弱持有最终资产对象。 */
struct asset_entry {
  asset_key identity;
  std::optional<std::weak_ptr<const asset>> cached;
  asset_load_metrics metrics;
  std::size_t source_bytes{};
  bool constructing{}; // 同步创建回调的递归保护。
  std::shared_ptr<load_operation> loading;
};
}
