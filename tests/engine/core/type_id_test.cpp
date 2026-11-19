// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <array>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/core/base/type_id.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
gneiss::type_id runtime_type_from_other_unit();
int main() {
  using namespace gneiss;
  static_assert(!std::is_convertible_v<type_id, gneiss::guid>);
  static_assert(!std::is_convertible_v<gneiss::guid, type_id>);
  if (runtime_type_of<int[2][3]>() == runtime_type_of<int[2][4]>())
    return 2;
  const auto text = runtime_type_of<std::string>();
  if (!text || text != runtime_type_from_other_unit() ||
      text != runtime_type_of<const std::string&>() || text == runtime_type_of<int>() ||
      runtime_type_of<void>())
    return 1;
  std::array<type_id, 8> ids{};
  std::vector<std::thread> threads;
  for (std::size_t i = 0; i < ids.size(); ++i)
    threads.emplace_back([&, i] {
      constexpr std::string_view key = "gneiss.test.concurrent.type";
      ids[i] = detail::intern_runtime_type(key.data(), key.size());
    });
  for (auto& thread : threads)
    thread.join();
  for (const auto id : ids)
    if (!id || id != ids.front() || id == text)
      return 3;
  type_id previous{};
  for (int i = 0; i < 256; ++i) {
    auto key = std::string{"gneiss.test.dynamic."} + std::to_string(i);
    const auto id = detail::intern_runtime_type(key.data(), key.size());
    if (!id || id == previous || id == text ||
        detail::intern_runtime_type(key.data(), key.size()) != id)
      return 4;
    previous = id;
  }
  if (runtime_type_of<std::string>() != text || runtime_type_from_other_unit() != text)
    return 5;
  return 0;
}
