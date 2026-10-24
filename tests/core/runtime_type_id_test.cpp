// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/runtime_type_id.hpp"
#include <array>
#include <string>
#include <thread>
#include <vector>
gneiss::engine::runtime_type_id runtime_type_from_other_unit();
int main() {
  using namespace gneiss::engine;
  static_assert(detail::canonical_cpp_type_name<int[2][3]>() !=
                detail::canonical_cpp_type_name<int[2][4]>());
  const auto text = runtime_type_of<std::string>();
  if (!text || text != runtime_type_from_other_unit() ||
      text != runtime_type_of<const std::string&>() || text == runtime_type_of<int>() ||
      runtime_type_of<void>() || !runtime_type_name({}).empty())
    return 1;
  const auto name = runtime_type_name(text);
  if (name.empty())
    return 2;
  std::array<runtime_type_id, 8> ids{};
  std::vector<std::thread> threads;
  for (std::size_t i = 0; i < ids.size(); ++i)
    threads.emplace_back([&, i] {
      constexpr auto key = detail::canonical_cpp_type_name<std::vector<long>>();
      ids[i] = detail::intern_runtime_type(key.data(), key.size());
    });
  for (auto& thread : threads)
    thread.join();
  for (const auto id : ids)
    if (!id || id != ids.front() || id == text)
      return 3;
  for (int i = 0; i < 256; ++i) {
    auto key = std::string{"gneiss.test.dynamic."} + std::to_string(i);
    const auto id = detail::intern_runtime_type(key.data(), key.size());
    if (runtime_type_name(id) != key || detail::intern_runtime_type(key.data(), key.size()) != id)
      return 4;
  }
  if (runtime_type_name(text) != name || runtime_type_name(text).data() != name.data())
    return 5;
  return 0;
}
