// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_INSPECTION_IPC_INSPECTION_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_INSPECTION_IPC_INSPECTION_PROTOCOL_H_

#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/resource/scene/component_description.hpp>
#include <gneiss/protocol/message.hpp>

#include <gneiss/engine/core/math/camera.hpp>
#include <gneiss/engine/core/math/transform.hpp>
#include <gneiss/engine/error.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gneiss::host {

inline constexpr std::uint16_t ipc_inspection_domain_version = 3U;
enum class ipc_inspection_operation : std::uint16_t { snapshot = 1U, resync = 2U };

/** 单次 Runtime 会话内的对象标识；跨会话或 generation 不同时不得复用。 */
struct ipc_runtime_object_id final {
  static constexpr auto type_guid = guid::literal("b16b6884-f862-4cce-8562-e166c2565eb7");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_runtime_object_id";
  std::uint64_t value = 0U;
  std::uint32_t generation = 0U;

  [[nodiscard]] bool is_valid() const noexcept { return value != 0U && generation != 0U; }
  [[nodiscard]] bool operator==(const ipc_runtime_object_id&) const noexcept = default;
};

/** 检查增量的会话与顺序标记。序号从 1 开始并在会话内严格递增。 */
struct ipc_inspection_stamp final {
  static constexpr auto type_guid = guid::literal("a34b8632-fa18-4a1b-b784-fad60e86df71");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_inspection_stamp";
  std::uint64_t session_id = 0U;
  std::uint64_t sequence = 0U;
};

enum class ipc_inspection_change_type : std::uint8_t { upsert, remove };

struct ipc_inspection_node final {
  static constexpr auto type_guid = guid::literal("15098de5-6d8a-4e74-b75c-987ee0881f10");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_inspection_node";
  ipc_runtime_object_id id;
  ipc_runtime_object_id parent;
  gneiss::guid uuid;
  gneiss::guid prefab_instance_uuid;
  gneiss::guid prefab_source_node_uuid;
  std::string name;
  gneiss::math::transform local_transform;
  gneiss::component_descriptions components{};
};

struct ipc_inspection_change final {
  static constexpr auto type_guid = guid::literal("6efad12b-b7fc-4345-9f9e-051958cbb44b");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_inspection_change";
  ipc_inspection_change_type type = ipc_inspection_change_type::upsert;
  ipc_runtime_object_id id;
  ipc_inspection_node node;
};

struct ipc_inspection_batch final {
  static constexpr auto type_guid = guid::literal("f87ebf96-7cbf-4ee1-be2f-05d07a06c52c");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_inspection_batch";
  ipc_inspection_stamp stamp;
  bool is_full = false;
  std::uint32_t chunk_index = 0U;
  std::uint32_t chunk_count = 1U;
  std::vector<ipc_inspection_change> changes;
};

extern const protocol::message_descriptor<ipc_inspection_batch> inspection_snapshot;

/** 将有界检查批次编码为 JSON 载荷。 */
[[nodiscard]] gneiss::error encode_ipc_inspection_batch(const ipc_inspection_batch& batch,
                                                        std::vector<std::uint8_t>& output);

/** 按协议负载上限把一个逻辑批次编码为有界分片。 */
[[nodiscard]] gneiss::error
encode_ipc_inspection_batch_chunks(const ipc_inspection_batch& batch,
                                   std::vector<std::vector<std::uint8_t>>& output);

/** 解码并验证 JSON 载荷，不验证镜像中的父子引用。 */
[[nodiscard]] gneiss::error decode_ipc_inspection_batch(std::span<const std::uint8_t> payload,
                                                        ipc_inspection_batch& output);

[[nodiscard]] gneiss::error
encode_ipc_inspection_batch_v2(const ipc_inspection_batch& batch,
                               std::vector<gneiss::ipc::envelope>& output);
[[nodiscard]] gneiss::error decode_ipc_inspection_batch_v2(const gneiss::ipc::envelope& envelope,
                                                           ipc_inspection_batch& output);
[[nodiscard]] gneiss::error encode_ipc_inspection_resync(std::uint32_t request_id,
                                                         gneiss::ipc::envelope& output) noexcept;
[[nodiscard]] gneiss::error
decode_ipc_inspection_resync(const gneiss::ipc::envelope& envelope) noexcept;
[[nodiscard]] std::span<const gneiss::protocol::operation_descriptor>
ipc_inspection_operations() noexcept;

} // namespace gneiss::host

#endif
