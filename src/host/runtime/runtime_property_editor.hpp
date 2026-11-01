// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_PROPERTY_EDITOR_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_PROPERTY_EDITOR_H_

#include "engine/core/error.hpp"
#include "engine/core/meta/registry.hpp"
#include "ipc_property_protocol.hpp"
#include "runtime_scene_inspection.hpp"

#include "engine/function/world/world.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <tuple>

namespace gneiss::host {

/** Runtime 主线程属性命令执行器；借用 World，须在其销毁前 reset。 */
class runtime_property_editor final {
public:
  runtime_property_editor() = default;
  ~runtime_property_editor();

  runtime_property_editor(const runtime_property_editor&) = delete;
  runtime_property_editor& operator=(const runtime_property_editor&) = delete;

  [[nodiscard]] gneiss::engine::error initialize(gneiss::engine::world& world,
                                                 runtime_scene_inspection& inspection,
                                                 std::uint64_t session_id) noexcept;
  [[nodiscard]] gneiss::engine::error execute(const ipc_property_write& command,
                                              ipc_property_write_result& output) noexcept;
  void reset() noexcept;

private:
  struct property_key final {
    ipc_runtime_object_id object;
    std::array<std::uint8_t, 16> type_id{};
    std::uint32_t field_id = 0U;

    [[nodiscard]] bool operator<(const property_key& other) const noexcept {
      return std::tie(object.value, object.generation, type_id, field_id) <
             std::tie(other.object.value, other.object.generation, other.type_id, other.field_id);
    }
  };

  gneiss::engine::world* world_ = nullptr;
  runtime_scene_inspection* inspection_ = nullptr;
  std::uint64_t session_id_ = 0U;
  bool initialized_ = false;
  std::map<property_key, std::uint64_t> revisions_;
};

} // namespace gneiss::host

#endif
