// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include <optional>
#include <string>
namespace gneiss::test_support {
template <class T, class D, class Diagnostic>
auto take_codec(engine::result<T, D> value, T& output, Diagnostic& diagnostic)
    -> std::optional<decltype(value.error().error)> {
  if (!value) {
    auto failure = std::move(value).error();
    auto code = failure.error;
    if constexpr (std::is_same_v<Diagnostic, std::string>)
      diagnostic = std::move(failure.message);
    else
      diagnostic = std::move(failure);
    output = {};
    return code;
  }
  diagnostic = {};
  output = std::move(*value);
  return std::nullopt;
}
}
