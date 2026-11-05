// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/meta/detail/json_value_codec.hpp"
#include "engine/resource/scene/detail/description_codec.hpp"
#include "engine/resource/scene/prefab_description.hpp"

#include <yyjson.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

namespace {

using gneiss::engine::scene_diagnostic;

void fail(scene_diagnostic& diagnostic, gneiss::engine::error result, std::string_view path,
          std::string_view message, std::size_t byte_offset = 0) noexcept {
  diagnostic.error = result;
  diagnostic.byte_offset = byte_offset;
  try {
    diagnostic.path = path;
    diagnostic.message = message;
  } catch (...) {
    diagnostic.error = gneiss::engine::error::out_of_memory;
    diagnostic.path.clear();
    diagnostic.message.clear();
  }
}

[[nodiscard]] std::string_view json_string(yyjson_val* value) {
  return {yyjson_get_str(value), yyjson_get_len(value)};
}

[[nodiscard]] bool read_required_string(yyjson_val* object, const char* name, std::string_view path,
                                        std::string& output, scene_diagnostic& diagnostic) {
  yyjson_val* value = yyjson_obj_get(object, name);
  const std::string field_path = std::string(path) + "/" + name;
  if (!yyjson_is_str(value) || yyjson_get_len(value) == 0U) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, field_path, "字段必须是非空字符串");
    return false;
  }
  output = json_string(value);
  return true;
}

[[nodiscard]] bool is_canonical_uuid(std::string_view value) {
  if (value.size() != 36U) {
    return false;
  }
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (value[index] != '-') {
        return false;
      }
    } else if ((value[index] < '0' || value[index] > '9') &&
               (value[index] < 'a' || value[index] > 'f')) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool read_required_guid(yyjson_val* object, const char* name, std::string_view path,
                                      gneiss::engine::guid& output, scene_diagnostic& diagnostic) {
  std::string text;
  if (!read_required_string(object, name, path, text, diagnostic))
    return false;
  const auto parsed = gneiss::engine::guid::parse(text);
  if (!is_canonical_uuid(text) || !parsed || !*parsed) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, std::string(path) + "/" + name,
         "UUID 必须是非零的规范小写形式");
    return false;
  }
  output = *parsed;
  return true;
}

yyjson_mut_val* make_uuid(yyjson_mut_doc* doc, gneiss::engine::guid value) {
  const auto text = value.string();
  return yyjson_mut_strncpy(doc, text.data(), text.size());
}

[[nodiscard]] bool parse_override_value(yyjson_val* object, gneiss::engine::value& output) {
  auto decoded = gneiss::engine::meta::detail::read_tagged_value(object);
  if (!decoded)
    return false;
  output = std::move(*decoded);
  return true;
}

[[nodiscard]] bool parse_prefab_override(yyjson_val* value,
                                         gneiss::engine::prefab_property_override& output,
                                         scene_diagnostic& diagnostic) {
  const std::string path;
  if (!yyjson_is_obj(value)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path, "覆盖必须是对象");
    return false;
  }
  gneiss::engine::guid source_node_uuid;
  if (!read_required_guid(value, "source_node_uuid", path, source_node_uuid, diagnostic) ||
      !source_node_uuid) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/source_node_uuid",
           "源节点 UUID 必须是小写规范形式");
    }
    return false;
  }
  gneiss::engine::guid type_id{};
  if (!read_required_guid(value, "type_guid", path, type_id, diagnostic)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/type_guid",
         "类型 GUID 必须是非零的规范小写 UUID");
    return false;
  }
  auto* field = yyjson_obj_get(value, "property_id");
  if (!yyjson_is_uint(field) || yyjson_get_uint(field) == 0U ||
      yyjson_get_uint(field) > std::numeric_limits<std::uint32_t>::max()) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/property_id",
         "Field ID 必须是非零 uint32");
    return false;
  }
  gneiss::engine::value parsed_value;
  if (!parse_override_value(yyjson_obj_get(value, "value"), parsed_value)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/value", "覆盖属性值无效");
    return false;
  }
  output = {
      .key =
          {
              .source_node_uuid = std::move(source_node_uuid),
              .type_guid = type_id,
              .property_id = gneiss::engine::meta::property_id{static_cast<std::uint32_t>(
                  yyjson_get_uint(field))},
          },
      .value = std::move(parsed_value),
  };
  if (auto* steps = yyjson_obj_get(value, "path")) {
    if (!yyjson_is_arr(steps) || yyjson_arr_size(steps) > 64) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/path", "覆盖路径无效");
      return false;
    }
    size_t i, count;
    yyjson_val* step;
    yyjson_arr_foreach(steps, i, count, step) {
      auto* property = yyjson_obj_get(step, "property_id");
      auto* index = yyjson_obj_get(step, "index");
      if (!yyjson_is_obj(step) || yyjson_obj_size(step) != 1 ||
          (property && (!yyjson_is_uint(property) || !yyjson_get_uint(property) ||
                        yyjson_get_uint(property) > UINT32_MAX)) ||
          (index && (!yyjson_is_uint(index) || yyjson_get_uint(index) > SIZE_MAX)) ||
          (!property && !index)) {
        fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/path",
             "路径步骤必须是 property_id 或 index");
        return false;
      }
      if (property)
        output.key.path.emplace_back(gneiss::engine::meta::property_id{
            static_cast<std::uint32_t>(yyjson_get_uint(property))});
      else
        output.key.path.emplace_back(static_cast<std::size_t>(yyjson_get_uint(index)));
    }
  }
  return true;
}

// 从当前描述生成已注册字段。
[[nodiscard]] yyjson_mut_val* make_override_value(yyjson_mut_doc* document,
                                                  const gneiss::engine::value& source) {
  auto encoded = gneiss::engine::meta::detail::write_tagged_value(document, source);
  return encoded ? *encoded : nullptr;
}

[[nodiscard]] yyjson_mut_val*
make_prefab_overrides(yyjson_mut_doc* document,
                      const std::vector<gneiss::engine::prefab_property_override>& overrides) {
  auto* array = yyjson_mut_arr(document);
  if (array == nullptr) {
    return nullptr;
  }
  for (const auto& source : overrides) {
    auto* object = yyjson_mut_obj(document);
    const auto type_text = source.key.type_guid.string();
    auto* value = make_override_value(document, source.value);
    if (object == nullptr || value == nullptr ||
        !yyjson_mut_obj_add_val(document, object, "source_node_uuid",
                                make_uuid(document, source.key.source_node_uuid)) ||
        !yyjson_mut_obj_add_strncpy(document, object, "type_guid", type_text.data(),
                                    type_text.size()) ||
        !yyjson_mut_obj_add_uint(document, object, "property_id", source.key.property_id.get()) ||
        !yyjson_mut_obj_add_val(document, object, "value", value) ||
        !yyjson_mut_arr_add_val(array, object)) {
      return nullptr;
    }
    if (!source.key.path.empty()) {
      auto* steps = yyjson_mut_arr(document);
      if (!steps || !yyjson_mut_obj_add_val(document, object, "path", steps))
        return nullptr;
      for (const auto& step : source.key.path) {
        auto* entry = yyjson_mut_obj(document);
        if (!entry || !yyjson_mut_arr_add_val(steps, entry))
          return nullptr;
        if (const auto* property = std::get_if<gneiss::engine::meta::property_id>(&step)) {
          if (!yyjson_mut_obj_add_uint(document, entry, "property_id", property->get()))
            return nullptr;
        } else if (!yyjson_mut_obj_add_uint(document, entry, "index", std::get<std::size_t>(step)))
          return nullptr;
      }
    }
  }
  return array;
}

using gneiss::engine::error;
using gneiss::engine::result;
namespace meta = gneiss::engine::meta;
template <class T, auto Parse>
result<T, meta::serialization_diagnostic> decode_override(std::span<const std::byte> bytes) {
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
      yyjson_read(reinterpret_cast<const char*>(bytes.data()), bytes.size(), 0), &yyjson_doc_free);
  if (!doc)
    return meta::serialization_diagnostic{.error = error::invalid_argument};
  T output;
  scene_diagnostic diagnostic;
  if (!Parse(yyjson_doc_get_root(doc.get()), output, diagnostic))
    return meta::serialization_diagnostic{diagnostic.byte_offset, std::move(diagnostic.path),
                                          std::move(diagnostic.message), diagnostic.error};
  return output;
}
template <class T>
result<std::vector<std::byte>, meta::serialization_diagnostic> encode_override(const T& input) {
  using namespace gneiss::engine;
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc(yyjson_mut_doc_new(nullptr),
                                                                      &yyjson_mut_doc_free);
  if (!doc)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  auto* array = make_prefab_overrides(doc.get(), {input});
  if (!array)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  yyjson_mut_doc_set_root(doc.get(), yyjson_mut_arr_remove_first(array));
  std::size_t length{};
  std::unique_ptr<char, decltype(&std::free)> text(yyjson_mut_write(doc.get(), 0, &length),
                                                   &std::free);
  if (!text)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  auto bytes = std::as_bytes(std::span{text.get(), length});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}
} // namespace

namespace gneiss::engine::detail {
result<std::vector<std::byte>, meta::serialization_diagnostic>
encode_property_override(const prefab_property_override& value) {
  return encode_override(value);
}
result<prefab_property_override, meta::serialization_diagnostic>
decode_property_override(std::span<const std::byte> bytes) {
  return decode_override<prefab_property_override, parse_prefab_override>(bytes);
}
}
