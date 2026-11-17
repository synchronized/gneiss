// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_ASSET_IPC_ASSET_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_ASSET_IPC_ASSET_PROTOCOL_H_

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/message.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gneiss::host {

inline constexpr std::uint16_t ipc_asset_domain_version = 5U;
inline constexpr std::size_t ipc_asset_max_payload_size = 256U * 1024U;

enum class ipc_asset_operation : std::uint16_t {
  reload = 1U,
  resync = 2U,
  cancel = 3U,
  progress = 4U
};
enum class ipc_asset_type : std::uint8_t { texture, material, static_mesh, scene, prefab };
enum class ipc_asset_apply_status : std::uint8_t {
  applied,
  failed,
  stale,
  restart_required,
  preparing,
  uploading,
  cancelled,
};

struct ipc_asset_revision final {
  static constexpr auto type_guid = guid::literal("7bc36746-67ab-48d1-8e06-5d8a399188b8");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_asset_revision";

  std::string uri;
  ipc_asset_type type{ipc_asset_type::texture};
};

/** Editor 发往 Runtime 的已提交资产修订；正文由 Runtime 通过工程文件服务读取。 */
struct ipc_asset_reload_request final {
  static constexpr auto type_guid = guid::literal("bc641a86-85ee-4b17-a0af-86bd43efea5f");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_asset_reload_request";

  std::uint64_t session_id = 0U;
  std::uint64_t revision = 0U;
  std::vector<ipc_asset_revision> assets;
};

/** 单个资源的结果；失败不表示它原有的内容失效。 */
struct ipc_asset_item_result final {
  static constexpr auto type_guid = guid::literal("bc74a2ab-4f4d-4a5a-b936-426cd6472aa4");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_asset_item_result";

  ipc_asset_revision asset;
  gneiss::error operation{gneiss::error::not_ready};
  bool applied{};
};
/** Runtime 对请求的汇总及逐项结果；汇总失败不撤回成功项。 */
struct ipc_asset_reload_result final {
  static constexpr auto type_guid = guid::literal("98a4523e-b83b-4bde-996f-88139666dbb2");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_asset_reload_result";

  std::uint64_t session_id = 0U;
  std::uint64_t revision = 0U;
  ipc_asset_apply_status status{ipc_asset_apply_status::failed};
  std::string message;
  bool can_cancel = false;
  std::uint32_t completed_assets = 0U;
  std::uint32_t total_assets = 0U;
  std::vector<ipc_asset_item_result> items{};
};

/** 取消只携带批次身份，终态仍由原请求返回。 */
struct ipc_asset_cancel_request final {
  static constexpr auto type_guid = guid::literal("e8ba12ac-93f1-4ed8-8709-bdd4ac191405");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_asset_cancel_request";
  std::uint64_t session_id{};
  std::uint64_t revision{};
};
extern const protocol::message_descriptor<ipc_asset_reload_request> asset_reload, asset_resync;
extern const protocol::message_descriptor<ipc_asset_cancel_request> asset_cancel;
extern const protocol::message_descriptor<ipc_asset_reload_result> asset_reload_result,
    asset_resync_result, asset_progress;
[[nodiscard]] std::span<const protocol::operation_descriptor> ipc_asset_operations() noexcept;
}
#endif
