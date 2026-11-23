// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/builtin_registration.hpp>
#include <gneiss/engine/resource/builtin_resource_registration.hpp>
#include <mutex>
#include <new>
namespace gneiss {
error initialize_meta(meta_registration_callback callback) {
  static std::mutex mutex;
  static bool attempted = false;
  static error status = error::not_ready;
  const std::scoped_lock lock(mutex);
  if (attempted)
    return callback ? error::invalid_state : status;
  attempted = true;
  // 预期失败保留终态；意外异常向执行边界传播，不允许后续调用误报成功。
  status = error::internal;

  status = gneiss::serialization::register_builtin_types();
  if (status.ok())
    status = register_builtin_resource_types();
  if (status.ok() && callback)
    status = callback();
  if (status.ok())
    status = meta::freeze();
  return status;
}
}
