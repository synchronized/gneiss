// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_INSPECTION_IPC_INSPECTION_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_INSPECTION_IPC_INSPECTION_PROTOCOL_H_

#include "ipc_dispatcher.h"

#include <gneiss/core/result.hpp>
#include <gneiss/scene.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gneiss {

inline constexpr std::uint16_t ipc_inspection_domain_version = 1U;
enum class ipc_inspection_operation : std::uint16_t { snapshot = 1U, resync = 2U };

/** 单次 Runtime 会话内的对象标识；跨会话或 generation 不同时不得复用。 */
struct ipc_runtime_object_id final {
  std::uint64_t value = 0U;
  std::uint32_t generation = 0U;

  [[nodiscard]] bool is_valid() const noexcept { return value != 0U && generation != 0U; }
  [[nodiscard]] bool operator==(const ipc_runtime_object_id&) const noexcept = default;
};

/** 检查增量的会话与顺序标记。序号从 1 开始并在会话内严格递增。 */
struct ipc_inspection_stamp final {
  std::uint64_t session_id = 0U;
  std::uint64_t sequence = 0U;
};

enum class ipc_inspection_sequence_result : std::uint8_t {
  accepted,
  duplicate,
  gap,
  stale_session,
  invalid,
};

/** Editor 侧检查消息顺序跟踪器；发现缺口后由上层请求完整快照。 */
class ipc_inspection_sequence_tracker final {
public:
  [[nodiscard]] result begin(std::uint64_t session_id, std::uint64_t first_sequence = 1U) noexcept;
  void reset() noexcept;
  [[nodiscard]] ipc_inspection_sequence_result observe(ipc_inspection_stamp stamp) noexcept;
  [[nodiscard]] std::uint64_t session_id() const noexcept { return session_id_; }
  [[nodiscard]] std::uint64_t next_sequence() const noexcept { return next_sequence_; }

private:
  std::uint64_t session_id_ = 0U;
  std::uint64_t next_sequence_ = 0U;
};

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

/** 将有界检查批次编码为 JSON 载荷。 */
[[nodiscard]] result encode_ipc_inspection_batch(const ipc_inspection_batch& batch,
                                                 std::vector<std::uint8_t>& output) noexcept;

/** 按协议负载上限把一个逻辑批次编码为有界分片。 */
[[nodiscard]] result
encode_ipc_inspection_batch_chunks(const ipc_inspection_batch& batch,
                                   std::vector<std::vector<std::uint8_t>>& output) noexcept;

/** 解码并验证 JSON 载荷，不验证镜像中的父子引用。 */
[[nodiscard]] result decode_ipc_inspection_batch(std::span<const std::uint8_t> payload,
                                                 ipc_inspection_batch& output) noexcept;

[[nodiscard]] result encode_ipc_inspection_batch_v2(const ipc_inspection_batch& batch,
                                                    std::vector<ipc_envelope>& output) noexcept;
[[nodiscard]] result decode_ipc_inspection_batch_v2(const ipc_envelope& envelope,
                                                    ipc_inspection_batch& output) noexcept;
[[nodiscard]] result encode_ipc_inspection_resync(std::uint32_t request_id,
                                                  ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_inspection_resync(const ipc_envelope& envelope) noexcept;
[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_inspection_operations() noexcept;

} // namespace gneiss

#endif
