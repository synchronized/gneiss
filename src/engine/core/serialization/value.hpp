// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/serialization/context.hpp"
namespace gneiss::serialization {
/** 捕获独立的数据树；内嵌引用按内容捕获，外部引用由注册策略转换为身份。 */
GNEISS_API result<gneiss::value, diagnostic> capture_value(const write_context&,
                                                           const meta::const_instance&);
GNEISS_API result<gneiss::value, diagnostic> capture_value(const meta::const_instance&);
/** 恢复到候选对象；失败可留下部分修改，提交与回滚由编辑操作控制。 */
GNEISS_API diagnostic apply_value(const read_context&, const meta::instance&, const gneiss::value&);
GNEISS_API diagnostic apply_value(const meta::instance&, const gneiss::value&);

/** 有类型的捕获入口；与视图入口共享相同 ops 和引用策略。 */
template <class T>
  requires(!std::same_as<std::remove_cvref_t<T>, meta::const_instance> &&
           !std::same_as<std::remove_cvref_t<T>, meta::instance>)
result<gneiss::value, diagnostic> capture_value(const write_context& context, const T& source) {
  auto registry = context.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  auto view = meta::view(*registry, std::as_const(source));
  if (!view)
    return diagnostic{.error = view.error()};
  return capture_value(context, *view);
}
template <class T>
  requires(!std::same_as<std::remove_cvref_t<T>, meta::const_instance> &&
           !std::same_as<std::remove_cvref_t<T>, meta::instance>)
result<gneiss::value, diagnostic> capture_value(const T& source) {
  return capture_value(write_context{}, source);
}
/** 创建独立候选后恢复；失败不会暴露部分初始化对象。 */
template <class T>
result<T, diagnostic> restore_value(const read_context& context, const gneiss::value& source) {
  auto registry = context.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  T candidate{};
  auto view = meta::view(*registry, candidate);
  if (!view)
    return diagnostic{.error = view.error()};
  auto status = apply_value(context, *view, source);
  if (status.error.failed())
    return status;
  return candidate;
}
template <class T> result<T, diagnostic> restore_value(const gneiss::value& source) {
  return restore_value<T>(read_context{}, source);
}
}
