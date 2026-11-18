// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/traits/type_identity.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/core/types/string_name.hpp>
#include <gneiss/engine/core/types/type_id.hpp>

#include <array>
#include <string>
#include <unordered_set>
#if defined(GNEISS_IDENTITY_TEST_THREADS)
#include <thread>
#endif

struct identified_asset {};
struct unnamed_asset {};
#if defined(GNEISS_IDENTITY_TEST_MODULE)
#ifdef _WIN32
#define IDENTITY_IMPORT __declspec(dllimport)
#else
#define IDENTITY_IMPORT
#endif
IDENTITY_IMPORT gneiss::type_id module_integer_identity();
IDENTITY_IMPORT gneiss::string_name module_string_identity();
#endif
namespace gneiss {
template <> struct type_identity<identified_asset> {
  static constexpr std::string_view name() noexcept { return "test.identified_asset"; }
  static constexpr gneiss::guid guid() noexcept { return gneiss::guid::from_bytes({1, 2, 3, 4}); }
};
}

// 编译期与运行期使用同一规则，字节顺序保持稳定。
using gneiss::guid;
constexpr auto literal_guid = guid::literal("00112233-4455-6677-8899-AABBCCDDEEFF");
static_assert(literal_guid == guid::from_bytes({0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                                0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}));
static_assert(guid::literal("00000000-0000-0000-0000-000000000000") == guid::nil());
static_assert(guid::literal("01020000-0000-0000-0000-000000000000") == guid::from_bytes({1, 2}));
static_assert(!guid::parse("00112233_4455-6677-8899-aabbccddeeff"));
static_assert(!guid::parse("00112233-4455-6677-8899-aabbccddeefg"));
static_assert(!guid::parse("00112233445566778899aabbccddeeff"));
static_assert(!guid::parse("00112233-4455-6677-8899-aabbccddeeff0"));
static_assert(!guid::parse(""));

int main() {
  using namespace gneiss;
#if defined(GNEISS_IDENTITY_TEST_MODULE)
  const auto integer = runtime_type_of<int>();
  if (module_integer_identity() != integer ||
      module_string_identity() != string_name{"identity.test.module"})
    return 9;
#endif
  const auto parsed = guid::parse("00112233-4455-6677-8899-AABBCCDDEEFF");
  if (!parsed || *parsed != literal_guid ||
      parsed->string() != "00112233-4455-6677-8899-aabbccddeeff" ||
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
