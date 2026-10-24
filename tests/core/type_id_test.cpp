// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/type_guid.hpp"
#include "engine/core/type_id.hpp"
#include <array>
#include <string>
#include <thread>
#include <vector>
gneiss::engine::type_id runtime_type_from_other_unit();
int main() {
  using namespace gneiss::engine;
  static_assert(!std::is_convertible_v<type_id, type_guid>);
  static_assert(!std::is_convertible_v<type_guid, type_id>);
  static_assert(detail::canonical_cpp_type_name<int[2][3]>() !=
                detail::canonical_cpp_type_name<int[2][4]>());
  const auto text = runtime_type_of<std::string>();
  if (!text || text != runtime_type_from_other_unit() ||
      text != runtime_type_of<const std::string&>() || text == runtime_type_of<int>() ||
      runtime_type_of<void>())
    return 1;
  std::array<type_id, 8> ids{};
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
