// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/guid.hpp"
#include "engine/core/string_name.hpp"
#include "engine/core/type_identity.hpp"

#include <array>
#include <string>
#include <unordered_set>
#if defined(GNEISS_IDENTITY_TEST_THREADS)
#include <thread>
#endif

struct identified_asset {};
struct unnamed_asset {};
namespace gneiss::engine {
template <> struct type_identity<identified_asset> {
  static constexpr std::string_view name() noexcept { return "test.identified_asset"; }
  static constexpr type_guid guid() noexcept {
    return type_guid{engine::guid::from_bytes({1, 2, 3, 4})};
  }
};
}

int main() {
  using namespace gneiss::engine;
  const auto parsed = guid::parse("00112233-4455-6677-8899-AABBCCDDEEFF");
  if (!parsed || parsed->string() != "00112233-4455-6677-8899-aabbccddeeff" ||
      guid::parse("00112233_4455-6677-8899-aabbccddeeff") ||
      guid::parse("00112233-4455-6677-8899-aabbccddeefg") || guid::parse("") || !guid{}.is_nil() ||
      guid::from_bytes(parsed->bytes()) != *parsed)
    return 1;
  if (guid_hash{}(*parsed) != guid_hash{}(guid::from_bytes(parsed->bytes())))
    return 2;
  if (string_name{} || string_name{nullptr} || string_name{""} ||
      string_name::find("identity.test.not-interned"))
    return 3;
  string_name held;
  {
    std::string temporary = "identity.test.owned";
    held = string_name{temporary};
    temporary.assign("changed");
  }
  if (held.view() != "identity.test.owned" || string_name::find(held.view()) != held ||
      held != string_name{"identity.test.owned"})
    return 4;
  const string_name binary{std::string_view{"a\0b", 3}};
  if (binary.view().size() != 3 || binary == string_name{"a"})
    return 5;
  std::unordered_set<string_name> names{held, string_name{"identity.test.owned"}, binary};
  if (names.size() != 2)
    return 6;
#if defined(GNEISS_IDENTITY_TEST_THREADS)
  std::array<string_name, 8> values;
  std::array<std::thread, 8> workers;
  for (std::size_t i = 0; i < workers.size(); ++i)
    workers[i] = std::thread([&, i] { values[i] = string_name{"identity.test.concurrent"}; });
  for (auto& worker : workers)
    worker.join();
  for (auto value : values)
    if (value != values[0])
      return 7;
#endif
  if (type_identity<const int&>::name() != "int" || type_identity<int>::guid() ||
      !type_identity<unnamed_asset>::name().empty() ||
      type_identity<identified_asset>::name() != "test.identified_asset" ||
      !type_identity<identified_asset>::guid())
    return 8;
  return 0;
}
