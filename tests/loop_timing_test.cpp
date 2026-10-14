// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/diagnostics/loop_timing.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition) {
  if (!condition) {
    std::abort();
  }
}
}
int main() try {
  using namespace gneiss::diagnostics;
  loop_timing summary;
  for (unsigned index = 1U; index <= 200U; ++index) {
    summary.add({.frame = index, .total_ms = static_cast<double>(index)});
  }
  check(summary.count() == 200U && summary.retained() == 128U && summary.maximum_ms() == 200.0);
  // 后到的短帧不能挤掉已经保留的长帧。
  summary.add({.frame = 201U, .total_ms = 0.1});
  std::ostringstream output;
  summary.write(output);
  check(output.str().find("sample,201,") == std::string::npos);
  check(output.str().find("sample,73,") != std::string::npos);
  check(output.str().find("sample,200,") != std::string::npos);
  check(output.str().find("maxima,201,0,200") != std::string::npos);
  check(output.str().find("over_100ms=100") != std::string::npos);
  check(active_loop_times == nullptr);
  // 单个短循环的阶段峰值不应被长时间等待循环挤掉。
  loop_record brief{.frame = 202U, .total_ms = 1.0};
  brief.stages[static_cast<std::size_t>(loop_stage::scene_builder_create)] = 0.5;
  summary.add(brief);
  std::ostringstream peaks;
  summary.write(peaks);
  check(peaks.str().find("sample,202,") == std::string::npos);
  check(peaks.str().find("peak:scene_builder_create,202,") != std::string::npos);
  loop_timing outer;
  const auto origin = timing_clock::now();
  {
    loop_frame frame(&outer, 1U, origin);
    auto* active = active_loop_times;
    check(active != nullptr);
    {
      loop_frame disabled(nullptr, 2U, origin);
      check(active_loop_times == nullptr);
    }
    check(active_loop_times == active);
    loop_timing inner;
    try {
      loop_frame nested(&inner, 3U, origin);
      check(active_loop_times != active);
      measure(loop_stage::update, [] { throw std::runtime_error("预期测试异常"); });
    } catch (const std::runtime_error&) {
      check(inner.count() == 1U && active_loop_times == active);
    }
    std::thread worker([] { check(active_loop_times == nullptr); });
    worker.join();
    int value = 3;
    auto& reference = measure(loop_stage::update, [&]() -> int& { return value; });
    check(&reference == &value);
  }
  check(active_loop_times == nullptr && outer.count() == 1U);
  return 0;
} catch (...) {
  return 1;
}
