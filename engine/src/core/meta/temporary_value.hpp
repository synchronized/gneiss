// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/base/assert.hpp>
#include <gneiss/engine/core/meta/ops/type_ops.hpp>
#include <new>

namespace gneiss::meta::detail {
class temporary_value {
public:
  explicit temporary_value(const type_ops& ops) : ops_(ops) {}
  temporary_value(const temporary_value&) = delete;
  temporary_value& operator=(const temporary_value&) = delete;
  ~temporary_value() {
    if (constructed_)
      ops_.destroy(data_);
    if (data_ && data_ != inline_)
      ::operator delete(data_, std::align_val_t{ops_.alignment});
  }
  template <class Construct> error construct(Construct&& construct) {
    GNEISS_ASSERT(!data_, "临时存储只能初始化一次");
    if (!ops_.size || !ops_.alignment || !ops_.destroy)
      return error::unsupported;
    // 常用标量和小值直接放栈上；大值及过度对齐类型才分配独立存储。
    data_ = ops_.size <= sizeof(inline_) && ops_.alignment <= alignof(std::max_align_t)
                ? static_cast<void*>(inline_)
                : ::operator new(ops_.size, std::align_val_t{ops_.alignment});
    auto status = construct(data_);
    constructed_ = status.ok();
    return status;
  }
  error construct_default() {
    if (!ops_.default_construct)
      return error::unsupported;
    return construct([&](void* p) {
      ops_.default_construct(p);
      return error::success;
    });
  }
  void* data() const noexcept { return data_; }

private:
  alignas(std::max_align_t) std::byte inline_[64];
  const type_ops& ops_;
  void* data_{};
  bool constructed_{};
};
}
