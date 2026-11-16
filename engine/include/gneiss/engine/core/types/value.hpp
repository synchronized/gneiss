// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/core/types/ref.hpp>
#include <gneiss/engine/core/math/types.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gneiss {
namespace meta {
class object;
}
/** 值类别使用显式分组编号；与存储分支下标及文件标签无关。 */
enum class value_kind : std::uint32_t {
  invalid = 0,
  boolean = 1,
  int64 = 2,
  uint64 = 3,
  float32 = 4,
  float64 = 5,
  string = 6,
  guid = 7,
  // 数学值
  vec2 = 20,
  vec3 = 21,
  vec4 = 22,
  quaternion = 23,
  matrix3 = 24,
  matrix4 = 25,
  record = 30,
  sequence = 31,
  map = 32,
  optional = 33,
  variant = 34,
  asset_reference = 40,
  object_reference = 41,
  // 对象引用
  object = 50,
};
/** 可直接通过标量 ops 读写的值类别；新增结构化分支不能自动成为标量。 */
[[nodiscard]] constexpr bool is_scalar_value_kind(value_kind kind) noexcept {
  switch (kind) {
  case value_kind::boolean:
  case value_kind::int64:
  case value_kind::uint64:
  case value_kind::float32:
  case value_kind::float64:
  case value_kind::string:
  case value_kind::guid:
  case value_kind::vec2:
  case value_kind::vec3:
  case value_kind::vec4:
  case value_kind::quaternion:
  case value_kind::matrix3:
  case value_kind::matrix4:
    return true;
  default:
    return false;
  }
}
/** 只读共享内容。构造复制输入，禁止接收外部共享指针或暴露可写别名。 */
template <class T> class immutable_value_storage {
public:
  immutable_value_storage() noexcept = default;
  explicit immutable_value_storage(const T& data) : data_(std::make_shared<const T>(data)) {}
  const T& get() const {
    if (data_)
      return *data_;
    static const T empty{};
    return empty;
  }
  bool operator==(const immutable_value_storage& other) const {
    return data_ == other.data_ || get() == other.get();
  }

private:
  std::shared_ptr<const T> data_;
};
struct record_value_data;
class record_value final : public immutable_value_storage<record_value_data> {
public:
  using immutable_value_storage<record_value_data>::immutable_value_storage;
  static constexpr std::string_view type_name = "record_value";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("ed0c7acb-923f-4f17-a2bb-69ae447ca33e");
};
struct sequence_value_data;
class sequence_value final : public immutable_value_storage<sequence_value_data> {
public:
  using immutable_value_storage<sequence_value_data>::immutable_value_storage;
  static constexpr std::string_view type_name = "sequence_value";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("ea84e6ba-4f29-4d8b-a7f4-d3353efb8f6f");
};
struct map_value_data;
class map_value final : public immutable_value_storage<map_value_data> {
public:
  using immutable_value_storage<map_value_data>::immutable_value_storage;
  static constexpr std::string_view type_name = "map_value";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("91d1e6c5-0b81-44fe-9a96-b08dc74814e7");
};
struct optional_value_data;
class optional_value final : public immutable_value_storage<optional_value_data> {
public:
  using immutable_value_storage<optional_value_data>::immutable_value_storage;
  static constexpr std::string_view type_name = "optional_value";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("d02aeb95-b261-4f50-9083-d4c281037671");
};
struct variant_value_data;
class variant_value final : public immutable_value_storage<variant_value_data> {
public:
  using immutable_value_storage<variant_value_data>::immutable_value_storage;
  static constexpr std::string_view type_name = "variant_value";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("d9d11420-44b5-46df-980d-4decd6261979");
};
/** 持久化身份，不持有运行时对象，也不复制引用目标。 */
struct asset_reference_value {
  static constexpr std::string_view type_name = "asset_reference_value";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("05d62e70-e0aa-48c8-b848-b0dead3ff471");
  gneiss::guid type;
  std::string uri;
  bool operator==(const asset_reference_value&) const = default;
};
/** 持久化身份，不持有运行时对象，也不复制引用目标。 */
struct object_reference_value {
  static constexpr std::string_view type_name = "object_reference_value";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("09958a5f-7541-4b35-ba02-61a4836aed12");
  gneiss::guid document;
  gneiss::guid object;
  bool operator==(const object_reference_value&) const = default;
};
/** 拥有数据的通用值；复制字符串后不依赖源对象。对象分支仅保存
 * ref<meta::object>，复制共享对象，不隐式深拷贝；不承载任意结构体或裸指针。
 * 结构化分支共享只读内容，修改必须替换节点；运行时对象引用不保证内容不可变。
 * 字符串构造和复制可能抛出分配异常，调用方在结果码边界转换错误。
 * 通用值不限制字符串编码或浮点范围；反射、文件和 IPC 各自执行边界校验。 */
struct value {
  static constexpr std::string_view type_name = "value";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("f9a2f886-1703-4138-806f-f10fc7f20efc");
  using storage_type =
      std::variant<std::monostate, bool, std::int64_t, std::uint64_t, float, double, std::string,
                   gneiss::guid, math::vector2, math::vector3, math::vector4, math::quaternion,
                   math::matrix3, math::matrix4, ref<meta::object>, record_value, sequence_value,
                   map_value, optional_value, variant_value, asset_reference_value,
                   object_reference_value>;

  static constexpr std::array kind_index = {value_kind::invalid,
                                            value_kind::boolean,
                                            value_kind::int64,
                                            value_kind::uint64,
                                            value_kind::float32,
                                            value_kind::float64,
                                            value_kind::string,
                                            value_kind::guid,
                                            value_kind::vec2,
                                            value_kind::vec3,
                                            value_kind::vec4,
                                            value_kind::quaternion,
                                            value_kind::matrix3,
                                            value_kind::matrix4,
                                            value_kind::object,
                                            value_kind::record,
                                            value_kind::sequence,
                                            value_kind::map,
                                            value_kind::optional,
                                            value_kind::variant,
                                            value_kind::asset_reference,
                                            value_kind::object_reference};
  static_assert(kind_index.size() == std::variant_size_v<storage_type>);
  storage_type payload;
  /** 对象按指针身份比较；其他分支按类型及分量精确比较；不执行浮点容差或旋转等价比较。 */
  [[nodiscard]] bool operator==(const value&) const = default;
  [[nodiscard]] value_kind kind() const noexcept {
    return payload.valueless_by_exception() ? value_kind::invalid : kind_index[payload.index()];
  }
};
struct value_property {
  static constexpr std::string_view type_name = "value_property";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("f2f4947b-cd95-49fc-9f78-1c82e33cd628");
  std::uint32_t id{};
  value content;
  bool operator==(const value_property&) const = default;
};
struct value_map_entry {
  static constexpr std::string_view type_name = "value_map_entry";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("8a948fce-a6de-4b8c-b588-c423aedcfc5c");
  value key;
  value content;
  bool operator==(const value_map_entry&) const = default;
};
struct record_value_data {
  static constexpr std::string_view type_name = "record_value_data";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("14a8d7f7-1ee9-49f0-9d56-6ab5eb50241d");
  gneiss::guid type;
  std::vector<value_property> properties;
  bool operator==(const record_value_data& other) const {
    if (type != other.type || properties.size() != other.properties.size())
      return false;
    for (const auto& field : properties) {
      auto found = std::ranges::find(other.properties, field.id, &value_property::id);
      if (found == other.properties.end() || found->content != field.content)
        return false;
    }
    return true;
  }
};
struct sequence_value_data {
  static constexpr std::string_view type_name = "sequence_value_data";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("220045a4-80c9-48db-9b52-d8f6427335e7");
  std::vector<value> elements;
  std::vector<guid> identities{};
  bool fixed{};
  bool keyed{};
  bool operator==(const sequence_value_data&) const = default;
};
struct map_value_data {
  static constexpr std::string_view type_name = "map_value_data";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("e4dc1713-4772-414a-b5d4-5e2ded30e45b");
  std::vector<value_map_entry> entries;
  bool operator==(const map_value_data& other) const {
    if (entries.size() != other.entries.size())
      return false;
    for (const auto& entry : entries) {
      auto found = std::ranges::find(other.entries, entry.key, &value_map_entry::key);
      if (found == other.entries.end() || found->content != entry.content)
        return false;
    }
    return true;
  }
};
struct optional_value_data {
  static constexpr std::string_view type_name = "optional_value_data";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("a2f11b16-2161-4c77-bc22-249686b7064c");
  std::optional<value> content;
  bool operator==(const optional_value_data&) const = default;
};
struct variant_value_data {
  static constexpr std::string_view type_name = "variant_value_data";
  static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("0002c777-d03d-4eb4-a935-2fe12d20f08d");
  std::string alternative;
  value content;
  bool operator==(const variant_value_data&) const = default;
};
/** 检查可持久化数据树；拒绝运行时对象引用、重复字段/键和超深嵌套。 */
[[nodiscard]] GNEISS_API error validate_persistent_value(const value&,
                                                         std::size_t maximum_depth = 64);
} // namespace gneiss
