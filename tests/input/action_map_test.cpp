// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/virtual_file_system.hpp"
#include "engine/function/input/action_map.hpp"
#include <cstring>

namespace {
class changing_files final : public gneiss::asset_internal::file_system {
public:
  std::string first, second;
  mutable unsigned reads = 0;
  bool fail_verify = false;
  gneiss_result read(std::string_view, std::vector<std::byte>&) const noexcept override {
    return GNEISS_ERROR_INTERNAL; // 测试禁止回退到无界读取。
  }
  gneiss_result read_bounded(std::string_view, std::size_t limit,
                             std::vector<std::byte>& output) const noexcept override {
    const auto& text = reads++ == 0 ? first : second;
    if (fail_verify && reads > 1)
      return GNEISS_ERROR_IO;
    if (text.size() > limit)
      return GNEISS_ERROR_INVALID_ARGUMENT;
    try {
      output.resize(text.size());
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    std::memcpy(output.data(), text.data(), text.size());
    return GNEISS_SUCCESS;
  }
};
}

int main() {
  gneiss::input_internal::action_map map;
  constexpr auto valid =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":-1.0},{"key":7,"scale":1.0}]}]})";
  if (gneiss::input_internal::parse_action_map(valid, map) != gneiss::result::success ||
      map.actions.size() != 1U || map.actions[0].bindings.size() != 2U) {
    return 1;
  }
  const auto original_size = map.actions[0].bindings.size();
  constexpr auto duplicate =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":1}]},{"name":"move","bindings":[{"key":7,"scale":1}]}]})";
  constexpr auto unknown_key =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":256,"scale":1}]}]})";
  constexpr auto invalid_scale =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":0}]}]})";
  if (gneiss::input_internal::parse_action_map(duplicate, map) !=
          gneiss::result::invalid_argument ||
      gneiss::input_internal::parse_action_map(unknown_key, map) !=
          gneiss::result::invalid_argument ||
      gneiss::input_internal::parse_action_map(invalid_scale, map) !=
          gneiss::result::invalid_argument ||
      map.actions[0].bindings.size() != original_size) {
    return 2;
  }
  auto backend = std::make_shared<changing_files>();
  gneiss::asset_internal::virtual_file_system files;
  if (files.mount("asset://", backend) != GNEISS_SUCCESS)
    return 3;
  backend->first = valid;
  backend->second = valid;
  if (gneiss::input_internal::load_action_map(files, "asset://input/map", map) !=
          gneiss::result::success ||
      backend->reads != 2)
    return 4;
  // 长度相同、内容变化，不能只校验文件大小；失败保留调用方原映射。
  backend->reads = 0;
  backend->second.replace(backend->second.find("move"), 4, "turn");
  if (gneiss::input_internal::load_action_map(files, "asset://input/map", map) !=
          gneiss::result::invalid_state ||
      map.actions[0].name != "move")
    return 5;
  backend->reads = 0;
  backend->fail_verify = true;
  if (gneiss::input_internal::load_action_map(files, "asset://input/map", map) !=
          gneiss::result::io ||
      map.actions[0].name != "move")
    return 6;
  backend->reads = 0;
  backend->first.assign(gneiss::input_internal::maximum_action_map_bytes + 1, ' ');
  if (gneiss::input_internal::load_action_map(files, "asset://input/map", map) !=
          gneiss::result::invalid_argument ||
      backend->reads != 1 || map.actions[0].name != "move")
    return 7;
  return 0;
}
