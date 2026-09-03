// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_IPC_INSPECTION_PROTOCOL_H_
#define GNEISS_SRC_IO_IPC_INSPECTION_PROTOCOL_H_

#include "ipc_protocol.h"

#include <gneiss/scene.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gneiss {

enum class ipc_inspection_change_type : std::uint8_t { upsert, remove };

struct ipc_inspection_node final {
  ipc_runtime_object_id id;
  ipc_runtime_object_id parent;
  std::string uuid;
  std::string prefab_instance_uuid;
  std::string prefab_source_node_uuid;
  std::string name;
  gneiss_transform local_transform = GNEISS_TRANSFORM_IDENTITY;
  std::uint32_t component_flags = 0U;
  gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  std::string mesh_uri;
  std::string material_uri;
};

struct ipc_inspection_change final {
  ipc_inspection_change_type type = ipc_inspection_change_type::upsert;
  ipc_runtime_object_id id;
  ipc_inspection_node node;
};

struct ipc_inspection_batch final {
  ipc_inspection_stamp stamp;
  bool is_full = false;
  std::uint32_t chunk_index = 0U;
  std::uint32_t chunk_count = 1U;
  std::vector<ipc_inspection_change> changes;
};

/** 将有界检查批次编码为 inspection_snapshot 帧。 */
[[nodiscard]] result encode_ipc_inspection_batch(const ipc_inspection_batch& batch,
                                                 ipc_frame& output) noexcept;

/** 按协议负载上限把一个逻辑批次编码为有界分片。 */
[[nodiscard]] result encode_ipc_inspection_batch_chunks(const ipc_inspection_batch& batch,
                                                        std::vector<ipc_frame>& output) noexcept;

/** 解码并验证 inspection_snapshot 帧，不验证镜像中的父子引用。 */
[[nodiscard]] result decode_ipc_inspection_batch(const ipc_frame& frame,
                                                 ipc_inspection_batch& output) noexcept;

} // namespace gneiss

#endif
