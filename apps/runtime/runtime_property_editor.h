// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_PROPERTY_EDITOR_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_PROPERTY_EDITOR_H_

#include "ipc_property_protocol.h"
#include "runtime_scene_inspection.h"

#include <gneiss/world.h>

#include <array>
#include <cstdint>
#include <map>
#include <tuple>

namespace gneiss::runtime_internal {

/** Runtime 主线程属性命令执行器。 */
class runtime_property_editor final {
public:
  runtime_property_editor() = default;
  ~runtime_property_editor();

  runtime_property_editor(const runtime_property_editor&) = delete;
  runtime_property_editor& operator=(const runtime_property_editor&) = delete;

  [[nodiscard]] result initialize(gneiss_world world, runtime_scene_inspection& inspection,
                                  std::uint64_t session_id) noexcept;
  [[nodiscard]] result execute(const ipc_property_write& command,
                               ipc_property_write_result& output) noexcept;
  void reset() noexcept;

private:
  struct property_key final {
    ipc_runtime_object_id object;
    std::array<std::uint8_t, 16> type_id{};
    gneiss_field_id field_id = GNEISS_NULL_FIELD_ID;

    [[nodiscard]] bool operator<(const property_key& other) const noexcept {
      return std::tie(object.value, object.generation, type_id, field_id) <
             std::tie(other.object.value, other.object.generation, other.type_id, other.field_id);
    }
  };

  gneiss_world world_ = GNEISS_NULL_WORLD;
  runtime_scene_inspection* inspection_ = nullptr;
  std::uint64_t session_id_ = 0U;
  gneiss_type_registry registry_ = GNEISS_NULL_TYPE_REGISTRY;
  std::map<property_key, std::uint64_t> revisions_;
};

} // namespace gneiss::runtime_internal

#endif
