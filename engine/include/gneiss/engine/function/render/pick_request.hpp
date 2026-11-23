// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/export.hpp>
#include <gneiss/engine/function/render/commands.hpp>
#include <memory>
#include <utility>
namespace gneiss::render {
/** 提交时的渲染身份；instance 为零表示成功未命中。消费方须验证对象仍存活。 */
struct pick_result {
  rid scene{};
  instance_key instance{};
};
/** 独立拾取请求，可跨服务关闭存活；查询与取消线程安全。
 * 取消只放弃结果，不提前释放在途 GPU 输入。副本共享结果，结果只能成功领取一次。 */
class pick_request final {
public:
  pick_request() = default;
  [[nodiscard]] GNEISS_ENGINE_API bool ready() const;
  GNEISS_ENGINE_API void cancel() const;
  /** 未完成返回 not_ready，取消返回 cancelled；空请求或重复领取返回 invalid_state。 */
  [[nodiscard]] GNEISS_ENGINE_API result<pick_result> take();

private:
  friend class service;
  struct state;
  explicit pick_request(std::shared_ptr<state> state) : state_(std::move(state)) {}
  std::shared_ptr<state> state_;
};
}
