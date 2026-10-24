// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/resource/types/action_map.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include <cstring>

namespace {
class changing_files final : public gneiss::engine::resource::file_system {
public:
  std::string first, second;
  mutable unsigned reads = 0;
  bool fail_verify = false;
  gneiss::engine::core::result read(std::string_view,
                                    std::vector<std::byte>&) const noexcept override {
    return gneiss::engine::core::result::internal; // 测试禁止回退到无界读取。
  }
  gneiss::engine::core::result
  read_bounded(std::string_view, std::size_t limit,
               std::vector<std::byte>& output) const noexcept override {
    const auto& text = reads++ == 0 ? first : second;
    if (fail_verify && reads > 1)
      return gneiss::engine::core::result::io;
    if (text.size() > limit)
      return gneiss::engine::core::result::invalid_argument;
    try {
      output.resize(text.size());
    } catch (...) {
      return gneiss::engine::core::result::out_of_memory;
    }
    std::memcpy(output.data(), text.data(), text.size());
    return gneiss::engine::core::result::success;
  }
};
}

int main() {
  gneiss::engine::resource::action_map map;
  constexpr auto valid =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":-1.0},{"key":7,"scale":1.0}]}]})";
  if (gneiss::engine::resource::parse_action_map(valid, map) !=
          gneiss::engine::core::result::success ||
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
  if (gneiss::engine::resource::parse_action_map(duplicate, map) !=
          gneiss::engine::core::result::invalid_argument ||
      gneiss::engine::resource::parse_action_map(unknown_key, map) !=
          gneiss::engine::core::result::invalid_argument ||
      gneiss::engine::resource::parse_action_map(invalid_scale, map) !=
          gneiss::engine::core::result::invalid_argument ||
      map.actions[0].bindings.size() != original_size) {
    return 2;
  }
  auto backend = std::make_shared<changing_files>();
  gneiss::engine::resource::virtual_file_system files;
  if (files.mount("asset://", backend) != gneiss::engine::core::result::success)
    return 3;
  backend->first = valid;
  backend->second = valid;
  if (gneiss::engine::resource::load_action_map(files, "asset://input/map", map) !=
          gneiss::engine::core::result::success ||
      backend->reads != 2)
    return 4;
  // 长度相同、内容变化，不能只校验文件大小；失败保留调用方原映射。
  backend->reads = 0;
  backend->second.replace(backend->second.find("move"), 4, "turn");
  if (gneiss::engine::resource::load_action_map(files, "asset://input/map", map) !=
          gneiss::engine::core::result::invalid_state ||
      map.actions[0].name != "move")
    return 5;
  backend->reads = 0;
  backend->fail_verify = true;
  if (gneiss::engine::resource::load_action_map(files, "asset://input/map", map) !=
          gneiss::engine::core::result::io ||
      map.actions[0].name != "move")
    return 6;
  backend->reads = 0;
  backend->first.assign(gneiss::engine::resource::maximum_action_map_bytes + 1, ' ');
  if (gneiss::engine::resource::load_action_map(files, "asset://input/map", map) !=
          gneiss::engine::core::result::invalid_argument ||
      backend->reads != 1 || map.actions[0].name != "move")
    return 7;
  return 0;
}
