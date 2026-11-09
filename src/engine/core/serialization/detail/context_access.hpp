// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/context.hpp"
#include <string>
#include <unordered_set>
namespace gneiss::engine::serialization::detail {
/** 每次最外层操作拥有的状态；嵌套 codec 入口借用，不进入公开配置对象。 */
struct operation_state {
  std::unordered_set<const void*> active;
  reader* input{};
  std::size_t depth{};
  std::string path;
};
struct context_access {
  static auto& state(const context& c) { return c.state_; }
  static auto& path(const context& c) { return c.state_->path; }
  static auto& active(const context& c) { return c.state_->active; }
  static auto& input(const context& c) { return c.state_->input; }
  static auto& depth(const context& c) { return c.state_->depth; }
};
/** 嵌套入口沿用已有状态；最外层结束时销毁状态，包括失败路径。 */
class operation_scope {
  const context& context_;
  operation_state local_;
  operation_state* previous_;
  std::size_t previous_depth_{};
  reader* previous_input_{};

public:
  explicit operation_scope(const context& c, bool fields_only = false)
      : context_(c), previous_(context_access::state(c)) {
    if (!previous_)
      context_access::state(c) = &local_;
    else {
      previous_depth_ = previous_->depth;
      previous_input_ = previous_->input;
      if (!fields_only)
        ++previous_->depth;
    }
  }
  ~operation_scope() {
    if (previous_) {
      previous_->depth = previous_depth_;
      previous_->input = previous_input_;
    }
    context_access::state(context_) = previous_;
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
