// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/sha256.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/types/asset_identity.hpp"
#include "engine/resource/types/material_parameters.hpp"
#include "engine/resource/types/mesh_data.hpp"
#include "engine/resource/types/texture_preparation_data.hpp"
#include <map>

#include "engine/core/result.hpp"
#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gneiss::engine {
class virtual_file_system;
struct material_description;
}

namespace gneiss::engine {

struct asset_diagnostic final {
  gneiss::engine::result result = gneiss::engine::result::success;
  std::size_t byte_offset = 0;
  std::string path;
  std::string message;
};

/** 纯 CPU 准备：不访问缓存、RID 表或 GPU；调用方拥有只读 VFS 快照及输出。
 * 有界读取需要后端支持；读取结果按格式校验，不提供来源版本一致性保证。 */
[[nodiscard]] gneiss::engine::result
prepare_texture(const gneiss::engine::virtual_file_system& file_system, std::string_view uri,
                gneiss::engine::prepared_texture_data& output, asset_diagnostic& diagnostic,
                std::size_t input_limit = std::numeric_limits<std::size_t>::max(),
                std::size_t output_limit = 256U * 1024U * 1024U,
                texture_prepare_profile profile = {}) noexcept;

enum class asset_type : std::uint32_t { invalid = 0U, mesh = 1U, material = 2U, texture = 3U };

struct asset_preparation_source final {
  std::string uri;
  asset_type type = asset_type::mesh;
};

/** 准备实际读取的输入；identity.type 使用所属资产类型，区分描述与负载的 URI。
 * 记录本次实际读取内容的摘要与长度；不是包含依赖及准备配置的完整缓存版本。 */
struct prepared_source final {
  asset_identity identity;
  std::uint64_t bytes{};
  gneiss::engine::sha256_digest digest{};
};

/** 材质输入由所属线程加载后传入；只读引用保活描述，准备器不查询缓存。 */
using material_descriptions =
    std::map<std::string, asset_ref<const material_description>, std::less<>>;

/** 后台准备结果；材质保留描述引用，纹理依赖使用 URI，不持有后端对象。 */
struct prepared_asset {
  asset_ref<const material_description> description;
  asset_preparation_source source;
  /** 描述在首位，纹理负载在其后；材质的资产依赖仍由 texture_uris 表示。 */
  std::vector<prepared_source> sources;
  gneiss::engine::mesh_data mesh;
  gneiss::engine::prepared_texture_data texture{};
  gneiss::engine::material_parameters material{};
  std::array<std::string, 5> texture_uris;
  std::size_t bytes{};
};
/** 已读输入的纯 CPU 解码；不访问真实 VFS。成功才替换输出。
 * Texture 传入描述字节、负载 URI 和负载；Mesh 只使用描述字节。
 * 选中纹理变体保留 CPU 数据，恢复上传无需读文件。 */
[[nodiscard]] result decode_loaded_asset(const asset_preparation_source& source,
                                         std::vector<std::byte> description,
                                         std::string payload_uri, std::vector<std::byte> payload,
                                         prepared_asset& output, asset_diagnostic& diagnostic,
                                         std::size_t maximum_bytes, texture_prepare_profile profile,
                                         asset_ref<const material_description> material = {},
                                         std::size_t material_bytes = 0U);

struct prepared_batch {
  std::vector<prepared_asset> assets;
  std::size_t bytes{};
  /** 整文件快照持有的输入字节；区间读取没有源副本，不计入此值。 */
  std::size_t input_bytes{};
};
/** 资产层的分步 CPU 准备；同一实例须串行推进。全部请求完成解析后输出，不重新读取来源。
 * 材质必须在 materials 中提供已解码描述，否则返回 unsupported，不读取材质文件。
 * 每步限制区间读取/摘要字节，解析仍是单项同步调用；旧整文件后端保留同步兼容行为。 */
class asset_preparation final {
public:
  struct limits {
    std::size_t material_bytes{};
    std::size_t maximum_assets{};
    std::size_t maximum_bytes{};
    bool deferred_reads{true};
    /** false 仅解析请求本身；依赖仍返回 URI，由调用方独立调度。 */
    bool include_dependencies{true};
  };
  asset_preparation(const virtual_file_system& files,
                    std::span<const asset_preparation_source> requested, limits budget,
                    texture_prepare_profile profile, material_descriptions materials = {});
  ~asset_preparation();
  [[nodiscard]] gneiss::engine::result advance(std::size_t byte_budget,
                                               const std::function<bool()>& cancelled,
                                               prepared_batch& output, asset_diagnostic& diagnostic,
                                               bool& complete,
                                               std::chrono::milliseconds wait_budget = {}) noexcept;

private:
  struct state;
  std::unique_ptr<state> state_;
};
/** material_bytes 由调用方指定单个材质发布所需的字节估算；含材质时不得为零。
 * 该值与依赖 URI 共同计入 maximum_bytes，溢出或预算不足拒绝整批。 */
[[nodiscard]] gneiss::engine::result
prepare_assets(const gneiss::engine::virtual_file_system& files,
               std::span<const asset_preparation_source> requested, prepared_batch& output,
               asset_diagnostic& diagnostic, const std::function<bool()>& cancelled,
               std::size_t material_bytes, std::size_t maximum_assets = 256U,
               std::size_t maximum_bytes = 256U * 1024U * 1024U,
               texture_prepare_profile profile = {}, material_descriptions materials = {}) noexcept;

} // namespace gneiss::engine
