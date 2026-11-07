// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/context.hpp"
namespace gneiss::engine::serialization::detail {
struct context_access {
  static auto& path(const context& c) { return c.path_; }
  static auto& active(const context& c) { return c.active_; }
  static auto& input(const context& c) { return c.input_; }
  static auto& depth(const context& c) { return c.depth_; }
  static auto& operations(const context& c) { return c.operations_; }
};
/** 嵌套入口沿用已有深度；外层结束（包括失败）清理所有运行状态。 */
class operation_scope {
  const context& context_;
  std::size_t previous_depth_;
  reader* previous_input_;

public:
  explicit operation_scope(const context& c, bool fields_only = false)
      : context_(c), previous_depth_(context_access::depth(c)),
        previous_input_(context_access::input(c)) {
    if (context_access::operations(c)++ == 0) {
      context_access::active(c).clear();
      context_access::path(c).clear();
      context_access::depth(c) = 0;
    } else if (!fields_only)
      ++context_access::depth(c);
  }
  ~operation_scope() {
    context_access::depth(context_) = previous_depth_;
    context_access::input(context_) = previous_input_;
    if (--context_access::operations(context_) == 0) {
      context_access::active(context_).clear();
      context_access::path(context_).clear();
      context_access::depth(context_) = 0;
    }
  }
  operation_scope(const operation_scope&) = delete;
};
class path_scope {
  const context& context_;
  std::size_t size_;

public:
  path_scope(const context& c, std::string_view segment)
      : context_(c), size_(context_access::path(c).size()) {
    context_access::path(c) += segment;
  }
  ~path_scope() { context_access::path(context_).resize(size_); }
};
class depth_scope {
  const context& context_;
  std::size_t previous_;

public:
  depth_scope(const context& c, std::size_t depth)
      : context_(c), previous_(context_access::depth(c)) {
    context_access::depth(c) = depth;
  }
  ~depth_scope() { context_access::depth(context_) = previous_; }
  depth_scope(const depth_scope&) = delete;
};
class active_reference {
  const context& context_;
  const void* address_;

public:
  active_reference(const context& c, const void* address) : context_(c), address_(address) {}
  ~active_reference() { context_access::active(context_).erase(address_); }
  active_reference(const active_reference&) = delete;
};
}
