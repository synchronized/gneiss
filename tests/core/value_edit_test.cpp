// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/value_edit.hpp"
#include <algorithm>
#include <array>
#include <iostream>
using namespace gneiss::engine;
int main() {
  const std::array ids{guid::literal("1583c0d5-412c-42d3-94bf-a5cd4c846101"),
                       guid::literal("1583c0d5-412c-42d3-94bf-a5cd4c846102"),
                       guid::literal("1583c0d5-412c-42d3-94bf-a5cd4c846103"),
                       guid::literal("1583c0d5-412c-42d3-94bf-a5cd4c846104")};
  auto list = [&](const std::vector<int>& order, bool modified) {
    sequence_value_data data;
    data.keyed = true;
    for (auto i : order) {
      data.identities.push_back(ids[std::size_t(i)]);
      data.elements.emplace_back(std::int64_t(i + (modified ? 10 : 0)));
    }
    return value{sequence_value{data}};
  };
  // 穷举不同子集与排列，验证增删、锚点重排、元素修改组合。
  std::vector<std::vector<int>> orders;
  for (unsigned mask = 0; mask < 16; ++mask) {
    std::vector<int> order;
    for (int i = 0; i < 4; ++i)
      if (mask & (1U << i))
        order.push_back(i);
    do {
      orders.push_back(order);
    } while (std::next_permutation(order.begin(), order.end()));
  }
  for (const auto& a : orders)
    for (const auto& b : orders) {
      auto before = list(a, false), after = list(b, true);
      auto edits = diff_values(before, after);
      if (!edits)
        return 1;
      auto result = apply_value_edits(before, *edits);
      if (!result || *result != after || before != list(a, false))
        return 2;
    }
  const auto initial = list({0, 1, 2}, false);
  const std::array<value_edit, 3> edits{{{{}, erase_value_operation{ids[0]}},
                                         {{ids[1]}, set_value_operation{value{std::int64_t{42}}}},
                                         {{}, move_value_operation{ids[2], ids[3]}}}};
  auto rejected = apply_value_edits(initial, edits);
  if (rejected || rejected.error().operation != 2 || initial != list({0, 1, 2}, false))
    return 3;
  auto replay = replay_value_edits(initial, edits);
  if (!replay || replay->conflicts.size() != 1 || replay->conflicts[0].operation != 2)
    return 4;
  auto data = std::get<sequence_value>(replay->root.payload).get();
  if (data.identities != std::vector{ids[1], ids[2]} || data.elements[0] != value{std::int64_t{42}})
    return 5;
  const std::array<value_edit, 1> erase_again{{{{}, erase_value_operation{ids[0]}}}};
  auto erased = apply_value_edits(replay->root, erase_again);
  if (!erased || *erased != replay->root)
    return 6;
  const std::array<value_edit, 1> index{{{{std::size_t{0}}, set_value_operation{value{true}}}}};
  if (apply_value_edits(initial, index))
    return 7;
  data.identities[1] = data.identities[0];
  if (validate_persistent_value(value{sequence_value{data}}).ok())
    return 8;
  const auto map_before =
      value{map_value{map_value_data{{{value{std::string{"a"}}, value{true}}}}}};
  const auto map_after =
      value{map_value{map_value_data{{{value{std::string{"b"}}, value{false}}}}}};
  auto diff = diff_values(map_before, map_after);
  if (!diff)
    return 9;
  auto updated = apply_value_edits(map_before, *diff);
  if (!updated || *updated != map_after)
    return 10;
  const auto plain = value{sequence_value{sequence_value_data{{value{true}}}}};
  const auto other = value{sequence_value{sequence_value_data{{value{false}}}}};
  auto replacement = diff_values(plain, other);
  if (!replacement || replacement->size() != 1 ||
      !std::holds_alternative<set_value_operation>((*replacement)[0].operation))
    return 11;
  return 0;
}
