// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <chrono>
#include <utility>
namespace gneiss {
using timing_clock = std::chrono::steady_clock;
class timing_span final {
public:
  explicit timing_span(double* output) noexcept
      : output_(output),
        started_(output_ != nullptr ? timing_clock::now() : timing_clock::time_point{}) {}
  ~timing_span() {
    if (output_ != nullptr) {
      *output_ += std::chrono::duration<double, std::milli>(timing_clock::now() - started_).count();
    }
  }
  timing_span(const timing_span&) = delete;
  timing_span& operator=(const timing_span&) = delete;

private:
  double* output_;
  timing_clock::time_point started_;
};

template <typename Function>
decltype(auto) measure(double* output,
                       Function&& function) noexcept(noexcept(std::forward<Function>(function)())) {
  const timing_span span(output);
  return std::forward<Function>(function)();
}

}
