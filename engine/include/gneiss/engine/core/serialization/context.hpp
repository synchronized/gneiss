// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/serialization/export.hpp>

#include <gneiss/engine/core/meta/registry_fwd.hpp>
#include <gneiss/engine/core/serialization/diagnostic.hpp>
#include <gneiss/engine/core/base/ref.hpp>
#include <gneiss/engine/core/base/type_id.hpp>
#include <gneiss/engine/result.hpp>

#include <string_view>

namespace gneiss::serialization {
class reader;
/** 外部引用解析器；收集阶段记录需求，绑定阶段返回已提交加载的身份。
 * 收集阶段可返回空引用；绑定阶段必须保证对象与 type 匹配，不等待内容加载。 */
class reference_resolver {
public:
  virtual ~reference_resolver() = default;
  /** 收集遍历允许不绑定对象；临时解码结果必须由调用方丢弃。 */
  virtual bool collecting() const noexcept { return false; }
  virtual result<ref<void>, diagnostic> resolve(type_id type, std::string_view uri,
                                                std::string_view path) = 0;
};
namespace detail {
struct context_access;
struct operation_state;
/** 不可复制的调用上下文；配置只读，运行状态私有。可顺序复用，不可并发共用。 */
class context_base {
  friend struct context_access;
  meta::registry_ref registry_;
  bool use_default_ = true;
  std::size_t maximum_depth_ = 64;
  mutable operation_state* state_{};

public:
  [[nodiscard]] GNEISS_SERIALIZATION_API std::string_view property_path() const noexcept;

protected:
  context_base() = default;
  ~context_base() = default;

public:
  /** 显式空注册表仍无效，不回退到默认表。 */
  explicit context_base(meta::registry_ref registry, std::size_t maximum_depth = 64) noexcept
      : registry_(std::move(registry)), use_default_(false), maximum_depth_(maximum_depth) {}
  explicit context_base(std::size_t maximum_depth) noexcept : maximum_depth_(maximum_depth) {}
  context_base(const context_base&) = delete;
  context_base& operator=(const context_base&) = delete;
  [[nodiscard]] GNEISS_SERIALIZATION_API result<meta::registry_ref> registry() const;
  [[nodiscard]] std::size_t maximum_depth() const noexcept { return maximum_depth_; }
};
}
/** 解码配置；解析器由调用方持有，必须覆盖整个读取操作。 */
class read_context final : public detail::context_base {
  reference_resolver* references_{};

public:
  using context_base::context_base;
  read_context() = default;
  explicit read_context(reference_resolver& references) noexcept : references_(&references) {}
  [[nodiscard]] reference_resolver* references() const noexcept { return references_; }
};
/** 编码配置；不接受解码专用的外部引用解析器。 */
class write_context final : public detail::context_base {
public:
  using context_base::context_base;
  write_context() = default;
};
}
