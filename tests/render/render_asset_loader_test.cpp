// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/resource/asset_manager.hpp"
#include "engine/resource/cache/resource_cache.hpp"
#include "engine/resource/loaders/material_serialization.hpp"
#include "engine/resource/loaders/mesh_binary.hpp"
#include "engine/resource/loaders/texture_binary.hpp"
#include "engine/resource/loaders/texture_container.hpp"
#include "engine/resource/loaders/texture_ktx2.hpp"
#include "engine/resource/vfs/file_system.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

#include <granit/asset_tools/texture_builder.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

class counting_material_decoder final : public gneiss::engine::asset_decoder {
public:
  mutable unsigned calls{};
  gneiss::engine::type_id target_type() const noexcept override {
    return gneiss::engine::runtime_type_of<gneiss::engine::material_description>();
  }
  std::size_t maximum_bytes() const noexcept override { return 64U * 1024U; }
  gneiss::engine::result
  decode(const std::vector<std::byte>& bytes, std::shared_ptr<const void>& output,
         gneiss::engine::serialization_diagnostic& diagnostic) const override {
    ++calls;
    auto value = std::make_shared<gneiss::engine::material_description>();
    auto status = gneiss::engine::decode_material(bytes, *value, diagnostic);
    if (status == gneiss::engine::result::success)
      output = std::move(value);
    return status;
  }
};

class memory_file_system final : public gneiss::engine::file_system {
public:
  std::unordered_map<std::string, std::string> files;
  mutable std::size_t read_count = 0;
  mutable std::size_t range_bytes{};
  mutable unsigned full_reads{};
  mutable bool change_on_reread{};
  gneiss::engine::result
  open_read(std::string_view path,
            std::unique_ptr<gneiss::engine::read_source>& output) const noexcept override {
    output.reset();
    const auto found = files.find(std::string(path));
    if (found == files.end())
      return gneiss::engine::result::not_found;
    class reader final : public gneiss::engine::read_source {
    public:
      reader(const std::string& bytes, const memory_file_system& owner)
          : bytes_(bytes), owner_(owner) {}
      std::uint64_t size() const noexcept override { return bytes_.size(); }
      gneiss::engine::result read_at(std::uint64_t offset,
                                     std::span<std::byte> output) const noexcept override {
        if (offset > bytes_.size() || output.size() > bytes_.size() - offset)
          return gneiss::engine::result::invalid_argument;
        for (std::size_t i = 0; i < output.size(); ++i)
          output[i] = static_cast<std::byte>(bytes_[static_cast<std::size_t>(offset) + i]);
        owner_.range_bytes += output.size();
        if (offset == 0U && output.size() == bytes_.size()) {
          ++owner_.full_reads;
          if (owner_.change_on_reread && owner_.full_reads > 2U && !output.empty())
            output.back() ^= std::byte{1};
        }
        return gneiss::engine::result::success;
      }

    private:
      const std::string& bytes_;
      const memory_file_system& owner_;
    };
    try {
      output = std::make_unique<reader>(found->second, *this);
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
    return gneiss::engine::result::success;
  }
  mutable bool change_during_check{};
  mutable unsigned bounded_reads{};
  [[nodiscard]] gneiss::engine::result
  read_bounded(std::string_view path, std::size_t limit,
               std::vector<std::byte>& bytes) const noexcept override {
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::engine::result::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::engine::result::invalid_argument;
    }
    const auto result = read(path, bytes);
    if (change_during_check && ++bounded_reads == 3U && !bytes.empty()) {
      bytes.push_back(std::byte{' '});
    }
    return result;
  }

  [[nodiscard]] gneiss::engine::result
  read(std::string_view path, std::vector<std::byte>& out_bytes) const noexcept override {
    ++read_count;
    try {
      const auto found = files.find(std::string(path));
      if (found == files.end()) {
        return gneiss::engine::result::not_found;
      }
      out_bytes.resize(found->second.size());
      for (std::size_t index = 0; index < found->second.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(found->second[index]);
      }
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
};

void add_indexed_mesh(memory_file_system& memory) {
  gneiss::engine::mesh_binary_data data{.vertices = {{.position = {-0.5F, -0.5F, 0.0F},
                                                      .texcoord = {-42469.96875F, 22.5F},
                                                      .normal = {0.0F, 0.0F, 1.0F}},
                                                     {.position = {0.5F, -0.5F, 0.0F},
                                                      .texcoord = {1.0F, 0.0F},
                                                      .normal = {0.0F, 0.0F, 1.0F}},
                                                     {.position = {0.5F, 0.5F, 0.0F},
                                                      .texcoord = {1.0F, 1.0F},
                                                      .normal = {0.0F, 0.0F, 1.0F}},
                                                     {.position = {-0.5F, 0.5F, 0.0F},
                                                      .texcoord = {0.0F, 1.0F},
                                                      .normal = {0.0F, 0.0F, 1.0F}}},
                                        .indices = {0U, 1U, 2U, 0U, 2U, 3U}};
  std::vector<std::byte> bytes;
  gneiss::engine::mesh_binary_diagnostic diagnostic;
  if (gneiss::engine::encode_mesh_binary(data, bytes, diagnostic) !=
      gneiss::engine::mesh_binary_result::success) {
    throw std::runtime_error{"无法创建索引 Mesh 测试数据"};
  }
  memory.files.emplace("models/indexed.gneiss-mesh",
                       std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

[[nodiscard]] bool
indexed_mesh_is_preserved(gneiss::engine::render_asset_loader& loader,
                          const gneiss::engine::render_resource_service& resources) {
  gneiss::engine::render_asset_ref mesh;
  gneiss::engine::asset_diagnostic diagnostic;
  if (loader.acquire_mesh("asset://models/indexed.gneiss-mesh", mesh, diagnostic) !=
      gneiss::engine::result::success) {
    return false;
  }
  const auto* resource = resources.get_mesh(gneiss::engine::asset_rid(mesh));
  return resource != nullptr && resource->vertices.size() == 4U && resource->normals.size() == 4U &&
         resource->indices.size() == 6U && resource->indices[5] == 3U &&
         resource->vertices[0].u == -42469.96875F && resource->vertices[0].v == 22.5F;
}

[[nodiscard]] std::vector<std::byte> make_runtime_texture() {
  const std::array payload = {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
  const std::array subresources{
      granit::asset_tools::texture::subresource_info{.mip_level = 0U,
                                                     .array_layer = 0U,
                                                     .data_offset = 0U,
                                                     .data_size = 4U,
                                                     .bytes_per_row = 4U,
                                                     .rows_per_image = 1U}};
  const std::array variants{granit::asset_tools::texture::variant_desc{
      .format = granit::texture_format::rgba8_srgb,
      .usage = granit::texture_usage::sampled | granit::texture_usage::transfer_destination,
      .payload = payload,
      .subresources = subresources}};
  const auto [status, built] = granit::asset_tools::texture::build({.variants = variants});
  if (status != granit::result::success)
    return {};
  std::vector<std::byte> output;
  std::string diagnostic;
  if (gneiss::engine::encode_texture_binary(built.manifest(), built.payload(), output,
                                            diagnostic) !=
      gneiss::engine::texture_binary_result::success) {
    return {};
  }
  return output;
}

void selected_variants() {
#if defined(GNEISS_HAS_GRANIT_PLATFORM)
  using namespace gneiss::engine;
  const auto require = [](bool value,
                          std::source_location where = std::source_location::current()) {
    if (!value) {
      std::fprintf(stderr, "variant test line %u\n", where.line());
      throw std::runtime_error("单变体加载测试失败");
    }
  };
  const std::array<std::byte, 16> bc{};
  const std::array<std::byte, 64> rgba{};
  const std::array bc_mips{granit::asset_tools::texture::subresource_info{
      .data_size = bc.size(), .bytes_per_row = 16U, .rows_per_image = 1U}};
  const std::array rgba_mips{granit::asset_tools::texture::subresource_info{
      .data_size = rgba.size(), .bytes_per_row = 16U, .rows_per_image = 4U}};
  constexpr auto usage =
      granit::texture_usage::sampled | granit::texture_usage::transfer_destination;
  const std::array variants{
      granit::asset_tools::texture::variant_desc{.format = granit::texture_format::bc7_rgba_srgb,
                                                 .usage = usage,
                                                 .payload = bc,
                                                 .subresources = bc_mips},
      granit::asset_tools::texture::variant_desc{.format = granit::texture_format::rgba8_srgb,
                                                 .usage = usage,
                                                 .payload = rgba,
                                                 .subresources = rgba_mips}};
  const auto [status, built] =
      granit::asset_tools::texture::build({.width = 4U, .height = 4U, .variants = variants});
  require(status == granit::result::success);
  std::vector<std::byte> binary;
  std::string message;
  require(
      gneiss::engine::encode_texture_binary(built.manifest(), built.payload(), binary, message) ==
      gneiss::engine::texture_binary_result::success);
  auto memory = std::make_shared<memory_file_system>();
  memory->files["t.gneiss-texture"] =
      std::string(reinterpret_cast<const char*>(binary.data()), binary.size());
  memory->files["t.texture.json"] =
      R"({"format":"gneiss.texture","version":1,"source":"asset://t.gneiss-texture","color_space":"srgb"})";
  gneiss::engine::virtual_file_system files;
  require(files.mount("asset://", memory) == gneiss::engine::result::success);
  const texture_prepare_profile compressed{.generation = 1U,
                                           .sampled_transfer_formats = {true, true, true, true}};
  const texture_prepare_profile fallback{.generation = 2U,
                                         .sampled_transfer_formats = {true, true, false, false}};
  gneiss::engine::prepared_texture_data texture;
  asset_diagnostic diagnostic;
  const auto limit = built.manifest().size() + bc.size();
  require(prepare_texture(files, "asset://t.texture.json", texture, diagnostic, limit, limit,
                          compressed) == gneiss::engine::result::success);
  require(texture.selected_variant == 0U && texture.payload.size() == bc.size() &&
          texture.profile == compressed && memory->range_bytes < binary.size() &&
          memory->read_count == 1U);
  require(prepare_texture(files, "asset://t.texture.json", texture, diagnostic, binary.size(),
                          binary.size(), fallback) == gneiss::engine::result::success);
  require(texture.selected_variant == 1U && texture.payload.size() == rgba.size() &&
          texture.profile == fallback);
  require(prepare_texture(files, "asset://t.texture.json", texture, diagnostic, limit, limit,
                          fallback) == gneiss::engine::result::out_of_memory &&
          texture.payload.empty());
  require(diagnostic.result == gneiss::engine::result::out_of_memory &&
          diagnostic.message.find("纹理准备预算不足") != std::string::npos &&
          diagnostic.message.find(std::to_string(rgba.size())) != std::string::npos &&
          diagnostic.message.find(std::to_string(limit)) != std::string::npos);
  require(prepare_texture(files, "asset://t.texture.json", texture, diagnostic, limit, limit,
                          {.generation = 3U}) == gneiss::engine::result::unsupported &&
          texture.manifest.empty());
  // 提高额度可重试；预算拒绝不能被缓存为永久失败。
  require(prepare_texture(files, "asset://t.texture.json", texture, diagnostic, binary.size(),
                          binary.size(), fallback) == gneiss::engine::result::success &&
          texture.payload.size() == rgba.size());
  // 只改选中负载，不改 Manifest，必须被内容摘要拒绝。
  memory->files["t.gneiss-texture"].back() ^= 1;
  require(prepare_texture(files, "asset://t.texture.json", texture, diagnostic, binary.size(),
                          binary.size(), fallback) == gneiss::engine::result::invalid_argument &&
          texture.payload.empty());
  memory->files["t.gneiss-texture"].back() ^= 1;
  const std::array requests{
      render_asset_reload{.uri = "asset://t.texture.json", .type = render_asset_type::texture}};
  render_resource_service resources;
  gneiss::engine::asset_manager asset_owner(files);
  [[maybe_unused]] auto& cache = asset_owner.cache();
  prepared_render_batch batch;
  require(prepare_render_assets(asset_owner, requests, batch, diagnostic, {}, 1U, limit,
                                compressed) == gneiss::engine::result::success);
  require(batch.bytes == limit && batch.assets.front().texture.profile == compressed);
  const auto* prepared_payload = batch.assets.front().texture.payload.data();
  const auto* prepared_manifest = batch.assets.front().texture.manifest.data();

  render_asset_loader loader(asset_owner, resources);
  std::array<render_asset_loader::asset_upload, 1> candidate;
  require(loader.prepare_upload(std::move(batch.assets.front()), candidate.front()) ==
          gneiss::engine::result::success);
  const auto old_frame = candidate.front().data.texture;
  require(candidate.front().data.texture_payload->data() == prepared_payload &&
          old_frame->manifest.data() == prepared_manifest && old_frame->profile == compressed &&
          old_frame->selected_variant == 0U);
  require(old_frame->payload.empty() && old_frame->payload_source &&
          !old_frame->upload_payload.expired());
  render_upload_item command;
  command.texture = old_frame;
  command.texture_payload = candidate.front().data.texture_payload;
  candidate.front().data.texture_payload.reset();
  require(!old_frame->upload_payload.expired());
  require(loader.complete_upload(candidate.front()) == gneiss::engine::result::success);
  command = {};
  require(old_frame->upload_payload.expired() && old_frame->payload.empty());
  std::vector<std::byte> restored;
  require(old_frame->payload_source->read(restored, bc.size()) == gneiss::engine::result::success &&
          restored.size() == bc.size());
  const auto payload_offset = binary.size() - built.payload().size();
  memory->files["t.gneiss-texture"][payload_offset] ^= 1;
  require(old_frame->payload_source->read(restored, bc.size()) ==
              gneiss::engine::result::invalid_state &&
          restored.empty());
  memory->files["t.gneiss-texture"][payload_offset] ^= 1;
  require(old_frame->payload_source->read(restored, bc.size()) == gneiss::engine::result::success);
  render_asset_ref lease;
  require(loader.acquire_cached(requests.front(), lease, compressed) ==
          gneiss::engine::result::success);
  require(loader.acquire_cached(requests.front(), lease, fallback) ==
              gneiss::engine::result::not_found &&
          !lease);
  require(prepare_render_assets(
              asset_owner, requests, batch, diagnostic, [] { return true; }, 1U, limit,
              compressed) == gneiss::engine::result::invalid_state &&
          batch.assets.empty());
  // 描述和负载各扫描一次；后续整文件读取会变化，但不会再发起来源复验。
  memory->full_reads = 0U;
  memory->change_on_reread = true;
  require(prepare_render_assets(asset_owner, requests, batch, diagnostic, {}, 1U, limit,
                                compressed) == gneiss::engine::result::success &&
          batch.assets.size() == 1U && memory->full_reads == 2U);
  memory->change_on_reread = false;
  unsigned cancellation_checks{};
  require(prepare_render_assets(
              asset_owner, requests, batch, diagnostic, [&] { return ++cancellation_checks == 2U; },
              1U, limit, compressed) == gneiss::engine::result::invalid_state &&
          batch.assets.empty());
  // 单纹理也走通用候选入口；移动后仍保留负载与设备身份。
  require(prepare_texture(files, "asset://t.texture.json", texture, diagnostic, binary.size(),
                          binary.size(), compressed) == gneiss::engine::result::success);
  const auto* single_payload = texture.payload.data();
  prepared_render_asset single_prepared;
  single_prepared.source = {"asset://single.texture.json", render_asset_type::texture};
  single_prepared.texture = std::move(texture);
  std::array<render_asset_loader::asset_upload, 1> single;
  require(loader.prepare_upload(std::move(single_prepared), single.front()) ==
          gneiss::engine::result::success);
  require(single.front().data.texture_payload &&
          single.front().data.texture_payload->data() == single_payload &&
          single.front().data.texture->payload.empty() &&
          single.front().data.texture->upload_payload.lock() ==
              single.front().data.texture_payload &&
          single.front().data.texture->profile == compressed &&
          single.front().data.texture->selected_variant == 0U);
  require(loader.complete_upload(single.front()) == gneiss::engine::result::success);

#endif
}

// 同步与异步更新都保留纯参数；准备上传不另建资源 RID。
bool material_parameter_roundtrip(gneiss::engine::virtual_file_system& files,
                                  memory_file_system& memory) {
  using namespace gneiss::engine;
  std::string json =
      R"({"format":"gneiss.material","version":5,"color":[0.25,0.5,0.75,0.5],"metallic":0.25,"roughness":0.75,"normal_scale":0.5,"occlusion_strength":0.25,"emissive":[0.25,0.5,0.75],"alpha_mode":"MASK","alpha_cutoff":0.25,"double_sided":true,)";
  constexpr std::array names{"base_color_texture", "metallic_roughness_texture", "normal_texture",
                             "occlusion_texture", "emissive_texture"};
  for (const auto* name : names) {
    json += "\"" + std::string(name) + "\":\"asset://textures/white.texture.json\",";
  }
  json += "\"sampling\":[";
  for (unsigned slot = 0; slot < 5U; ++slot) {
    if (slot != 0U) {
      json += ",";
    }
    json += "{\"uv_set\":" + std::to_string(slot % 2U) +
            ",\"mag_filter\":0,\"min_filter\":1,\"mip_filter\":2,\"address_u\":" +
            std::to_string(slot % 3U) + ",\"address_v\":1}";
  }
  json += "]}";
  memory.files["parameters.material.json"] = json;
  const auto matches = [](const material_resource* value) {
    if (value == nullptr || value->parameters.color[0] != 0.25F ||
        value->parameters.color[1] != 0.5F || value->parameters.color[2] != 0.75F ||
        value->parameters.color[3] != 0.5F || value->parameters.metallic != 0.25F ||
        value->parameters.roughness != 0.75F || value->parameters.normal_scale != 0.5F ||
        value->parameters.occlusion_strength != 0.25F ||
        value->parameters.emissive != std::array{0.25F, 0.5F, 0.75F} ||
        value->parameters.alpha_mode != gneiss::engine::material_alpha_mode::mask ||
        value->parameters.alpha_cutoff != 0.25F || value->parameters.double_sided != 1U) {
      return false;
    }
    const auto handles = value->texture_handles();
    for (std::size_t slot = 0; slot < handles.size(); ++slot) {
      const auto& sample = value->parameters.sampling[slot];
      if (handles[slot] == gneiss::engine::null_rid || sample.uv_set != slot % 2U ||
          static_cast<std::uint32_t>(sample.mag_filter) != 0U ||
          static_cast<std::uint32_t>(sample.min_filter) != 1U ||
          static_cast<std::uint32_t>(sample.mip_filter) != 2U ||
          static_cast<std::uint32_t>(sample.address_u) != slot % 3U ||
          static_cast<std::uint32_t>(sample.address_v) != 1U) {
        return false;
      }
    }
    return true;
  };
  render_resource_service resources;
  gneiss::engine::asset_manager asset_owner(files);
  [[maybe_unused]] auto& cache = asset_owner.cache();
  auto decoder = std::make_shared<counting_material_decoder>();
  if (asset_owner.register_decoder(decoder).failed())
    return false;
  render_asset_loader loader(asset_owner, resources);
  asset_diagnostic diagnostic;
  render_asset_ref synchronous;
  if (loader.acquire_material("asset://parameters.material.json", synchronous, diagnostic) !=
          gneiss::engine::result::success ||
      !matches(resources.get_material(gneiss::engine::asset_rid(synchronous)))) {
    return false;
  }

  prepared_render_batch batch;
  const std::array request{render_asset_reload{.uri = "asset://parameters.material.json",
                                               .type = render_asset_type::material}};
  const auto before = resources.live_resource_count();
  if (prepare_render_assets(asset_owner, request, batch, diagnostic, {}) !=
          gneiss::engine::result::success ||
      batch.assets.size() != 2U || resources.live_resource_count() != before ||
      batch.assets.back().bytes !=
          sizeof(material_resource) +
              5U * std::string_view("asset://textures/white.texture.json").size() ||
      batch.assets.back().material.color != std::array{0.25F, 0.5F, 0.75F, 0.5F}) {
    return false;
  }
  render_asset_loader::asset_upload missing;
  if (loader.prepare_upload(batch.assets.back(), missing) != gneiss::engine::result::success ||
      resources.live_resource_count() != before) {
    return false;
  }
  std::vector<render_asset_loader::asset_upload> candidates;
  for (auto& prepared : batch.assets) {
    render_asset_loader::asset_upload candidate;
    if (loader.prepare_upload(std::move(prepared), candidate) != gneiss::engine::result::success) {
      return false;
    }
    if (loader.complete_upload(candidate) != gneiss::engine::result::success)
      return false;
    candidates.push_back(std::move(candidate));
  }
  if (!matches(candidates.back().data.material.get()) ||
      !matches(resources.get_material(gneiss::engine::asset_rid(synchronous)))) {
    return false;
  }
  if (decoder->calls != 1U)
    return false;
  const auto original_rid = asset_rid(synchronous);
  if (loader.reload_assets(request, diagnostic).failed() || decoder->calls != 2U ||
      asset_rid(synchronous) != original_rid)
    return false;
  return true;
}

} // namespace

int main() try { // NOLINT(readability-function-cognitive-complexity)：集成测试按返回码定位阶段。
  selected_variants();
  static constexpr std::array<std::uint8_t, 68> png_bytes = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
      0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00,
      0x00, 0xb5, 0x1c, 0x0c, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78,
      0xda, 0x63, 0xfc, 0xff, 0x1f, 0x00, 0x03, 0x03, 0x02, 0x00, 0xef, 0xbf, 0x6b, 0x99,
      0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  auto memory = std::make_shared<memory_file_system>();
  memory->files.emplace(
      "models/triangle.mesh.json",
      R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[-0.5,-0.5,0],[0.5,-0.5,0],[0,0.5,0]]})");
  memory->files.emplace(
      "materials/default.material.json",
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"base_color_texture":null,"color":[0.25,0.5,0.75,1]})");
  memory->files.emplace("models/broken.mesh.json", "{");
  const std::string png_data(reinterpret_cast<const char*>(png_bytes.data()), png_bytes.size());
  memory->files.emplace("textures/white.png", png_data);
  memory->files.emplace(
      "textures/white.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://textures/white.png","color_space":"srgb"})");
  memory->files.emplace(
      "textures/missing.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://textures/missing.png","color_space":"linear"})");
  gneiss::engine::texture_ktx2 ktx_source{
      .transfer = gneiss::engine::texture_transfer::linear,
      .levels = {
          {.width = 2U, .height = 2U, .pixels = std::vector<std::byte>(16U, std::byte{0x40})},
          {.width = 1U, .height = 1U, .pixels = std::vector<std::byte>(4U, std::byte{0x20})}}};
  std::vector<std::byte> ktx_bytes;
  std::string ktx_diagnostic;
  if (gneiss::engine::encode_texture_ktx2(ktx_source, ktx_bytes, ktx_diagnostic) !=
      gneiss::engine::texture_ktx2_result::success) {
    return 24;
  }
  memory->files.emplace(
      "textures/linear.ktx2",
      std::string(reinterpret_cast<const char*>(ktx_bytes.data()), ktx_bytes.size()));
  memory->files.emplace(
      "textures/linear.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://textures/linear.ktx2","color_space":"linear"})");
  const auto packaged_bytes = make_runtime_texture();
  if (packaged_bytes.empty()) {
    return 26;
  }
  memory->files.emplace(
      "textures/packaged.gneiss-texture",
      std::string(reinterpret_cast<const char*>(packaged_bytes.data()), packaged_bytes.size()));
  memory->files.emplace(
      "textures/packaged.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://textures/packaged.gneiss-texture","color_space":"srgb"})");
  memory->files.emplace(
      "models/textured.mesh.json",
      R"({"format":"gneiss.mesh","version":2,"topology":"triangle_list","vertices":[[-0.5,-0.5,0],[0.5,-0.5,0],[0,0.5,0]],"uvs":[[0,0],[1,0],[0.5,1]]})");
  memory->files.emplace(
      "models/lit.mesh.json",
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.5,-0.5,0],[0.5,-0.5,0],[0,0.5,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})");
  memory->files.emplace(
      "models/invalid-normal.mesh.json",
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.5,-0.5,0],[0.5,-0.5,0],[0,0.5,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,2],[0,0,1],[0,0,1]]})");
  memory->files.emplace(
      "materials/textured.material.json",
      R"({"format":"gneiss.material","version":5,"color":[1,0.5,0.25,1],"base_color_texture":"asset://textures/white.texture.json","metallic":0.25,"roughness":0.75})");
  add_indexed_mesh(*memory);

  gneiss::engine::virtual_file_system file_system;
  gneiss::engine::render_resource_service resources;
  gneiss::engine::asset_manager asset_owner(file_system);
  [[maybe_unused]] auto& cache = asset_owner.cache();
  gneiss::engine::render_asset_loader loader(asset_owner, resources);
  if (file_system.mount("asset://", memory) != gneiss::engine::result::success) {
    return 1;
  }

  {
    auto parameter_files = std::make_shared<memory_file_system>();
    parameter_files->files = memory->files;
    gneiss::engine::virtual_file_system parameter_vfs;
    if (parameter_vfs.mount("asset://", parameter_files) != gneiss::engine::result::success ||
        !material_parameter_roundtrip(parameter_vfs, *parameter_files)) {
      return 40;
    }
  }

  gneiss::engine::render_asset_ref first_mesh;
  gneiss::engine::render_asset_ref second_mesh;
  gneiss::engine::render_asset_ref material;
  gneiss::engine::asset_diagnostic diagnostic;
  if (loader.acquire_mesh("asset://models/triangle.mesh.json", first_mesh, diagnostic) !=
          gneiss::engine::result::success ||
      loader.acquire_mesh("asset://models/triangle.mesh.json", second_mesh, diagnostic) !=
          gneiss::engine::result::success ||
      gneiss::engine::asset_rid(first_mesh) == gneiss::engine::null_rid ||
      gneiss::engine::asset_rid(first_mesh) != gneiss::engine::asset_rid(second_mesh) ||
      memory->read_count != 1U ||
      loader.acquire_material("asset://materials/default.material.json", material, diagnostic) !=
          gneiss::engine::result::success ||
      gneiss::engine::asset_rid(material) == gneiss::engine::null_rid ||
      resources.live_resource_count() != 2U) {
    return 2;
  }

  gneiss::engine::render_asset_ref wrong_type;
  if (loader.acquire_material("asset://models/triangle.mesh.json", wrong_type, diagnostic) !=
          gneiss::engine::result::invalid_argument ||
      wrong_type) {
    return 3;
  }

  gneiss::engine::render_asset_ref broken;
  if (loader.acquire_mesh("asset://models/broken.mesh.json", broken, diagnostic) !=
          gneiss::engine::result::invalid_argument ||
      diagnostic.byte_offset == 0U || broken) {
    return 4;
  }
  memory->files["models/broken.mesh.json"] =
      R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
  if (loader.acquire_mesh("asset://models/broken.mesh.json", broken, diagnostic) !=
          gneiss::engine::result::success ||
      gneiss::engine::asset_rid(broken) == gneiss::engine::null_rid ||
      resources.live_resource_count() != 3U) {
    return 5;
  }

  // 同步与任务准备共用解析；准备阶段不得触碰缓存或 RID。
  const auto before_resources = resources.live_resource_count();
  const auto before_cache = cache.size();
  gneiss::engine::prepared_texture_data prepared;
  for (const auto* uri :
       {"asset://textures/white.texture.json", "asset://textures/linear.texture.json",
        "asset://textures/packaged.texture.json"}) {
    auto expected = gneiss::engine::result::success;
#if !defined(GNEISS_HAS_GRANIT_PLATFORM)
    if (std::string_view(uri).ends_with("packaged.texture.json")) {
      expected = gneiss::engine::result::unsupported;
    }
#endif
    const auto result =
        gneiss::engine::prepare_texture(file_system, uri, prepared, diagnostic, 1024U * 1024U);
    if (expected == gneiss::engine::result::unsupported) {
      if (result != expected || prepared.width != 0U || diagnostic.path != "/source" ||
          resources.live_resource_count() != before_resources || cache.size() != before_cache) {
        return 43;
      }
      continue;
    }
    if (result != gneiss::engine::result::success || prepared.width == 0U ||
        resources.live_resource_count() != before_resources || cache.size() != before_cache) {
      return 40;
    }
  }
  if (gneiss::engine::prepare_texture(file_system, "asset://textures/white.texture.json", prepared,
                                      diagnostic, 1U) != gneiss::engine::result::invalid_argument) {
    return 41;
  }

  memory->change_during_check = true;
  memory->bounded_reads = 0U;
  if (gneiss::engine::prepare_texture(file_system, "asset://textures/white.texture.json", prepared,
                                      diagnostic,
                                      1024U * 1024U) != gneiss::engine::result::success ||
      prepared.levels.empty() || memory->bounded_reads != 2U) {
    return 42;
  }
  memory->change_during_check = false;

  gneiss::engine::render_asset_ref first_texture;
  gneiss::engine::render_asset_ref second_texture;
  if (loader.acquire_texture("asset://textures/white.texture.json", first_texture, diagnostic) !=
          gneiss::engine::result::success ||
      loader.acquire_texture("asset://textures/white.texture.json", second_texture, diagnostic) !=
          gneiss::engine::result::success ||
      gneiss::engine::asset_rid(first_texture) == gneiss::engine::null_rid ||
      gneiss::engine::asset_rid(first_texture) != gneiss::engine::asset_rid(second_texture)) {
    return 6;
  }
  const auto* texture = resources.get_texture(gneiss::engine::asset_rid(first_texture));
  if (texture == nullptr || texture->width != 1U || texture->height != 1U ||
      texture->color_space != gneiss::engine::texture_color_space::srgb ||
      texture->levels.size() != 1U || texture->levels.front().pixels.size() != 4U ||
      resources.live_resource_count() != 4U) {
    return 7;
  }

  gneiss::engine::render_asset_ref missing_texture;
  if (loader.acquire_texture("asset://textures/missing.texture.json", missing_texture,
                             diagnostic) != gneiss::engine::result::not_found ||
      missing_texture || diagnostic.path != "/source") {
    return 8;
  }
  memory->files.emplace("textures/missing.png", png_data);
  if (loader.acquire_texture("asset://textures/missing.texture.json", missing_texture,
                             diagnostic) != gneiss::engine::result::success ||
      gneiss::engine::asset_rid(missing_texture) == gneiss::engine::null_rid ||
      resources.live_resource_count() != 5U) {
    return 9;
  }

  gneiss::engine::render_asset_ref textured_mesh;
  gneiss::engine::render_asset_ref textured_material;
  if (loader.acquire_mesh("asset://models/textured.mesh.json", textured_mesh, diagnostic) !=
          gneiss::engine::result::success ||
      loader.acquire_material("asset://materials/textured.material.json", textured_material,
                              diagnostic) != gneiss::engine::result::success) {
    return 10;
  }
  const auto* mesh_resource = resources.get_mesh(gneiss::engine::asset_rid(textured_mesh));
  const auto* material_resource =
      resources.get_material(gneiss::engine::asset_rid(textured_material));
  if (mesh_resource == nullptr || mesh_resource->vertices.size() != 3U ||
      !mesh_resource->normals.empty() || mesh_resource->vertices[1].u != 1.0F ||
      mesh_resource->vertices[2].v != 1.0F || material_resource == nullptr ||
      material_resource->base_color_texture != gneiss::engine::asset_rid(first_texture) ||
      material_resource->parameters.metallic != 0.25F ||
      material_resource->parameters.roughness != 0.75F || resources.live_resource_count() != 7U) {
    return 11;
  }

  gneiss::engine::render_asset_ref lit_mesh;
  gneiss::engine::render_asset_ref invalid_normal_mesh;
  if (loader.acquire_mesh("asset://models/lit.mesh.json", lit_mesh, diagnostic) !=
      gneiss::engine::result::success) {
    return 13;
  }
  const auto* lit_resource = resources.get_mesh(gneiss::engine::asset_rid(lit_mesh));
  if (lit_resource == nullptr || lit_resource->normals.size() != 3U ||
      lit_resource->normals[0].z != 1.0F ||
      loader.acquire_mesh("asset://models/invalid-normal.mesh.json", invalid_normal_mesh,
                          diagnostic) != gneiss::engine::result::invalid_argument ||
      diagnostic.path != "/normals/0") {
    return 14;
  }
  // JSON 与二进制路径的平铺 UV 保持一致，后台准备不触碰缓存。
  {
    const auto cache_before = cache.size();
    const auto resources_before = resources.live_resource_count();
    constexpr std::string_view tiled_json =
        R"({"format":"gneiss.mesh","version":2,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[-42469.96875,22.5],[2,0],[0,-2]]})";
    memory->files["models/tiled.mesh.json"] = tiled_json;
    const std::array requests{
        gneiss::engine::render_asset_reload{"asset://models/tiled.mesh.json",
                                            gneiss::engine::render_asset_type::mesh},
        gneiss::engine::render_asset_reload{"asset://models/indexed.gneiss-mesh",
                                            gneiss::engine::render_asset_type::mesh}};
    gneiss::engine::prepared_render_batch batch;
    if (gneiss::engine::prepare_render_assets(asset_owner, requests, batch, diagnostic, {}) !=
            gneiss::engine::result::success ||
        batch.assets.size() != 2U || cache.size() != cache_before ||
        resources.live_resource_count() != resources_before) {
      return 40;
    }
    for (const auto& asset : batch.assets) {
      if (asset.mesh.vertices[0].u != -42469.96875F || asset.mesh.vertices[0].v != 22.5F) {
        return 41;
      }
    }
    gneiss::engine::render_asset_ref tiled;
    if (loader.acquire_mesh(requests[0].uri, tiled, diagnostic) !=
            gneiss::engine::result::success ||
        resources.get_mesh(gneiss::engine::asset_rid(tiled))->vertices[0].u != -42469.96875F) {
      return 42;
    }
    tiled = {};
    auto invalid_json = std::string(tiled_json);
    invalid_json.replace(invalid_json.find("-42469.96875"), 12U, "1e100");
    memory->files["models/tiled.mesh.json"] = invalid_json;
    // 使用重新加载绕过旧缓存，失败必须保持旧资源版本。
    if (loader.reload_assets(std::span(requests).first(1U), diagnostic) !=
            gneiss::engine::result::invalid_argument ||
        gneiss::engine::prepare_render_assets(asset_owner, requests, batch, diagnostic, {}) !=
            gneiss::engine::result::invalid_argument ||
        !batch.assets.empty()) {
      return 43;
    }
  }
  if (!indexed_mesh_is_preserved(loader, resources)) {
    return 15;
  }

  const auto old_mesh = gneiss::engine::asset_rid(textured_mesh);
  const auto old_material = gneiss::engine::asset_rid(textured_material);
  const auto old_texture = gneiss::engine::asset_rid(first_texture);
  const auto old_mesh_data = resources.share_mesh(old_mesh);
  const auto old_material_data = resources.share_material(old_material);
  const auto old_texture_data = resources.share_texture(old_texture);
  memory->files["models/textured.mesh.json"] =
      R"({"format":"gneiss.mesh","version":2,"topology":"triangle_list","vertices":[[-1,-1,0],[1,-1,0],[0,1,0]],"uvs":[[0,0],[1,0],[0.5,1]]})";
  memory->files["materials/textured.material.json"] =
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[0.2,0.4,0.6,1],"base_color_texture":"asset://textures/white.texture.json"})";
  const std::array reloads{
      gneiss::engine::render_asset_reload{.uri = "asset://models/textured.mesh.json",
                                          .type = gneiss::engine::render_asset_type::mesh},
      gneiss::engine::render_asset_reload{.uri = "asset://materials/textured.material.json",
                                          .type = gneiss::engine::render_asset_type::material},
      gneiss::engine::render_asset_reload{.uri = "asset://textures/white.texture.json",
                                          .type = gneiss::engine::render_asset_type::texture}};
  if (loader.reload_assets(reloads, diagnostic) != gneiss::engine::result::success) {
    return 16;
  }
  gneiss::engine::render_asset_ref reloaded_mesh;
  gneiss::engine::render_asset_ref reloaded_material;
  gneiss::engine::render_asset_ref reloaded_texture;
  if (loader.acquire_mesh(reloads[0].uri, reloaded_mesh, diagnostic) !=
          gneiss::engine::result::success ||
      loader.acquire_material(reloads[1].uri, reloaded_material, diagnostic) !=
          gneiss::engine::result::success ||
      loader.acquire_texture(reloads[2].uri, reloaded_texture, diagnostic) !=
          gneiss::engine::result::success ||
      gneiss::engine::asset_rid(reloaded_mesh) != old_mesh ||
      gneiss::engine::asset_rid(reloaded_material) != old_material ||
      gneiss::engine::asset_rid(reloaded_texture) != old_texture) {
    return 17;
  }
  const auto* reloaded_material_resource =
      resources.get_material(gneiss::engine::asset_rid(reloaded_material));
  if (reloaded_material_resource == nullptr ||
      reloaded_material_resource->base_color_texture !=
          gneiss::engine::asset_rid(reloaded_texture) ||
      resources.share_mesh(old_mesh) == old_mesh_data ||
      resources.share_material(old_material) == old_material_data ||
      resources.share_texture(old_texture) == old_texture_data ||
      old_material_data->parameters.color[0] == reloaded_material_resource->parameters.color[0] ||
      reloaded_material_resource->parameters.color[0] != 0.2F) {
    return 18;
  }
  const auto updated_mesh_data = resources.share_mesh(old_mesh);
  const auto updated_material_data = resources.share_material(old_material);
  const auto updated_texture_data = resources.share_texture(old_texture);
  memory->files["materials/textured.material.json"] = "{";
  auto& successful_mesh = memory->files["models/textured.mesh.json"];
  successful_mesh.replace(successful_mesh.find("[-1,-1,0]"), 9U, "[-2,-1,0]");
  if (loader.reload_assets(reloads, diagnostic) != gneiss::engine::result::invalid_argument) {
    return 19;
  }
  gneiss::engine::render_asset_ref after_failed_reload;
  if (loader.acquire_material(reloads[1].uri, after_failed_reload, diagnostic) !=
          gneiss::engine::result::success ||
      gneiss::engine::asset_rid(after_failed_reload) !=
          gneiss::engine::asset_rid(reloaded_material) ||
      resources.share_mesh(old_mesh) == updated_mesh_data ||
      resources.get_mesh(old_mesh)->vertices.front().x != -2.0F ||
      resources.share_material(old_material) != updated_material_data ||
      resources.share_texture(old_texture) == updated_texture_data) {
    return 20;
  }

  gneiss::engine::render_asset_ref ktx_texture;
  if (loader.acquire_texture("asset://textures/linear.texture.json", ktx_texture, diagnostic) !=
      gneiss::engine::result::success) {
    return 24;
  }
  const auto* ktx_resource = resources.get_texture(gneiss::engine::asset_rid(ktx_texture));
  if (ktx_resource == nullptr || ktx_resource->levels.size() != 2U ||
      ktx_resource->levels[1U].width != 1U ||
      ktx_resource->color_space != gneiss::engine::texture_color_space::linear) {
    return 25;
  }
  ktx_texture = {};

  gneiss::engine::render_asset_ref packaged_texture;
#if defined(GNEISS_HAS_GRANIT_PLATFORM)
  if (loader.acquire_texture("asset://textures/packaged.texture.json", packaged_texture,
                             diagnostic) != gneiss::engine::result::success) {
    return 27;
  }
  const auto* packaged_resource =
      resources.get_texture(gneiss::engine::asset_rid(packaged_texture));
  if (packaged_resource == nullptr || !packaged_resource->levels.empty() ||
      packaged_resource->manifest.empty() || packaged_resource->payload.size() != 4U) {
    return 28;
  }
  packaged_texture = {};
#else
  const auto live_before = resources.live_resource_count();
  if (loader.acquire_texture("asset://textures/packaged.texture.json", packaged_texture,
                             diagnostic) != gneiss::engine::result::unsupported ||
      packaged_texture || diagnostic.path != "/source" || diagnostic.message.empty() ||
      resources.live_resource_count() != live_before) {
    return 29;
  }
#endif

  first_mesh = {};
  second_mesh = {};
  material = {};
  broken = {};
  first_texture = {};
  second_texture = {};
  missing_texture = {};
  textured_mesh = {};
  lit_mesh = {};
  reloaded_mesh = {};
  reloaded_material = {};
  reloaded_texture = {};
  after_failed_reload = {};
  loader.release_unused();
  if (cache.size() != 3U) {
    return 21;
  }
  if (resources.live_resource_count() != 2U) {
    return 22;
  }
  if (resources.get_texture(old_texture) == nullptr) {
    return 23;
  }
  textured_material = {};
  loader.release_unused();
  if (cache.size() != 0U || resources.live_resource_count() != 0U) {
    return 13;
  }
  // 缓存已经销毁但调用方仍持有资源：最后释放先排队，再由所属线程撤销 RID。
  gneiss::engine::render_asset_ref retained;
  {
    gneiss::engine::asset_manager temporary_assets(file_system);
    gneiss::engine::render_asset_loader temporary_loader(temporary_assets, resources);
    if (temporary_loader.acquire_mesh("asset://models/triangle.mesh.json", retained, diagnostic) !=
        gneiss::engine::result::success)
      return 30;
  }
  const auto retained_rid = gneiss::engine::asset_rid(retained);
  retained.reset();
  if (resources.mesh_status(retained_rid) != gneiss::engine::result::success ||
      resources.drain_retired_resources() != 1U ||
      resources.mesh_status(retained_rid) != gneiss::engine::result::invalid_handle)
    return 31;
  // 服务先销毁时外部引用只能释放 CPU 外壳，不再解引用服务裸指针。
  {
    gneiss::engine::render_resource_service temporary_resources;
    gneiss::engine::asset_manager temporary_assets(file_system);
    gneiss::engine::render_asset_loader temporary_loader(temporary_assets, temporary_resources);
    if (temporary_loader.acquire_mesh("asset://models/triangle.mesh.json", retained, diagnostic) !=
        gneiss::engine::result::success)
      return 32;
  }
  retained.reset();
  return 0;
} catch (...) {
  return 99;
}
