// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/asset_build.h"
#include "tooling/asset_build/texture_mip_generator.h"

#include "tooling/asset_build/ktx2_probe.h"
#include "tooling/asset_build/runtime_texture_builder.h"

#include "engine/asset/mesh_binary.hpp"
#include "engine/asset/png_decoder.hpp"
#include "engine/asset/texture_ktx2.hpp"

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace gneiss::tooling::asset_build {
namespace {

struct source_node final {
  std::filesystem::path absolute_path;
  std::string relative_path;
  const processor_description* processor{};
  std::vector<std::string> dependencies;
  std::string cache_key;
  std::optional<asset_internal::texture_transfer> texture_transfer;
  std::optional<bool> normal_map{};
};

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::filesystem::path utf8_path(std::string_view text) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

[[nodiscard]] bool safe_relative_path(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  const auto path = utf8_path(text);
  return !path.is_absolute() && path == path.lexically_normal() && *path.begin() != "..";
}

[[nodiscard]] bool is_runtime_source(std::string_view relative) noexcept {
  return relative != "source" && !relative.starts_with("source/") && relative != ".gneiss" &&
         !relative.starts_with(".gneiss/");
}

[[nodiscard]] bool uri_character(char value) noexcept {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '_' || value == '-' || value == '.' ||
         value == '/';
}

[[nodiscard]] std::vector<std::string> scan_dependencies(const std::filesystem::path& path) {
  if (path.extension() != ".json") {
    return {};
  }
  std::ifstream stream(path, std::ios::binary);
  const std::string content{std::istreambuf_iterator<char>{stream},
                            std::istreambuf_iterator<char>{}};
  constexpr std::string_view prefix = "asset://";
  std::set<std::string> unique;
  auto cursor = std::string::size_type{};
  while ((cursor = content.find(prefix, cursor)) != std::string::npos) {
    const auto begin = cursor + prefix.size();
    auto end = begin;
    while (end < content.size() && uri_character(content[end])) {
      ++end;
    }
    const auto relative = std::string_view(content).substr(begin, end - begin);
    if (safe_relative_path(relative)) {
      unique.emplace(relative);
    }
    cursor = end;
  }
  return {unique.begin(), unique.end()};
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    return {};
  }
  const auto size = stream.tellg();
  if (size <= 0) {
    return {};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()), size);
  return stream.good() ? std::move(bytes) : std::vector<std::byte>{};
}

[[nodiscard]] bool valid_json(const std::filesystem::path& path) {
  const auto bytes = read_bytes(path);
  if (bytes.empty()) {
    return false;
  }
  auto* document =
      yyjson_read(reinterpret_cast<const char*>(bytes.data()), bytes.size(), YYJSON_READ_NOFLAG);
  if (document == nullptr) {
    return false;
  }
  yyjson_doc_free(document);
  return true;
}

[[nodiscard]] bool validate_source(const source_node& node) {
  const auto extension = node.absolute_path.extension().string();
  if (node.processor->id == "gneiss.texture") {
    if (extension == ".ktx2") {
      return inspect_ktx2(node.absolute_path).result == ktx2_probe_result::success;
    }
    const auto bytes = read_bytes(node.absolute_path);
    if (extension == ".png") {
      constexpr std::array<std::byte, 8U> signature = {
          std::byte{0x89U}, std::byte{0x50U}, std::byte{0x4EU}, std::byte{0x47U},
          std::byte{0x0DU}, std::byte{0x0AU}, std::byte{0x1AU}, std::byte{0x0AU}};
      return bytes.size() >= signature.size() &&
             std::equal(signature.begin(), signature.end(), bytes.begin());
    }
    return false;
  }
  if (node.processor->id == "gneiss.mesh" && extension == ".gneiss-mesh") {
    const auto bytes = read_bytes(node.absolute_path);
    asset_internal::mesh_binary_data output;
    asset_internal::mesh_binary_diagnostic diagnostic;
    return asset_internal::decode_mesh_binary(bytes, output, diagnostic) ==
           asset_internal::mesh_binary_result::success;
  }
  if ((node.processor->id == "gneiss.mesh" || node.processor->id == "gneiss.material" ||
       node.processor->id == "gneiss.document") &&
      extension == ".json") {
    return valid_json(node.absolute_path);
  }
  return true;
}

void hash_bytes(std::uint64_t& hash, std::string_view bytes) noexcept {
  constexpr std::uint64_t prime = 1099511628211ULL;
  for (const auto value : bytes) {
    hash ^= static_cast<std::uint8_t>(value);
    hash *= prime;
  }
}

[[nodiscard]] bool cache_key(const source_node& node, const build_request& request,
                             std::string& output) {
  std::ifstream stream(node.absolute_path, std::ios::binary);
  if (!stream) {
    return false;
  }
  std::uint64_t hash = 14695981039346656037ULL;
  std::array<char, 64U * 1024U> buffer{};
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    hash_bytes(hash, {buffer.data(), static_cast<std::size_t>(stream.gcount())});
  }
  if (!stream.eof()) {
    return false;
  }
  hash_bytes(hash, "\0");
  hash_bytes(hash, node.processor->id);
  hash_bytes(hash, "\0");
  hash_bytes(hash, std::to_string(node.processor->version));
  hash_bytes(hash, "\0");
  hash_bytes(hash, request.target_platform);
  hash_bytes(hash, "\0");
  hash_bytes(hash, request.target_architecture);
  hash_bytes(hash, "\0");
  hash_bytes(hash, request.profile == build_profile::shipping ? "shipping" : "development");
  hash_bytes(hash, node.normal_map.value_or(false) ? "normal" : "color-or-data");
  if (node.texture_transfer) {
    hash_bytes(hash, *node.texture_transfer == asset_internal::texture_transfer::srgb ? "srgb"
                                                                                      : "linear");
  }
  std::ostringstream text;
  text << std::hex << std::setfill('0') << std::setw(16) << hash;
  output = text.str();
  return true;
}

[[nodiscard]] build_report fail(build_result result, std::string diagnostic) {
  return {.result = result,
          .source_count = 0U,
          .built_count = 0U,
          .cache_hit_count = 0U,
          .pruned_count = 0U,
          .outputs = {},
          .diagnostic = std::move(diagnostic)};
}

[[nodiscard]] bool copy_asset_file(const std::filesystem::path& source,
                                   const std::filesystem::path& target) {
  std::error_code error;
  std::filesystem::create_directories(target.parent_path(), error);
  return !error &&
         std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, error) &&
         !error;
}

[[nodiscard]] std::string output_relative_path(const source_node& node) {
  if (node.processor->id == "gneiss.texture" && node.absolute_path.extension() == ".png") {
    auto path = utf8_path(node.relative_path);
    path.replace_extension(".gneiss-texture");
    return path_utf8(path);
  }
  return node.relative_path;
}

[[nodiscard]] bool cook_png(const source_node& node, std::vector<std::byte>& output,
                            std::string& diagnostic) {
  const auto bytes = read_bytes(node.absolute_path);
  asset_internal::decoded_png decoded;
  if (asset_internal::decode_png(bytes, decoded, diagnostic) != GNEISS_SUCCESS) {
    return false;
  }
  asset_internal::texture_ktx2 texture{
      .transfer = node.texture_transfer.value_or(asset_internal::texture_transfer::srgb),
      .levels = {
          {.width = decoded.width, .height = decoded.height, .pixels = std::move(decoded.pixels)}}};
  while (texture.levels.back().width != 1U || texture.levels.back().height != 1U) {
    const auto& previous = texture.levels.back();
    texture.levels.push_back({.width = std::max(1U, previous.width / 2U),
                              .height = std::max(1U, previous.height / 2U),
                              .pixels = generate_texture_mip(previous, texture.transfer,
                                                             node.normal_map.value_or(false))});
  }
  return build_runtime_texture(texture, output, diagnostic) ==
         runtime_texture_build_result::success;
}

[[nodiscard]] std::vector<std::byte> rewrite_png_uris(std::vector<std::byte> bytes) {
  std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::size_t cursor{};
  while ((cursor = text.find("asset://", cursor)) != std::string::npos) {
    const auto end = text.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-./", cursor + 8U);
    const auto uri_end = end == std::string::npos ? text.size() : end;
    if (uri_end >= 4U && text.compare(uri_end - 4U, 4U, ".png") == 0) {
      constexpr std::string_view replacement = ".gneiss-texture";
      text.replace(uri_end - 4U, 4U, replacement);
      cursor = uri_end - 4U + replacement.size();
    } else {
      cursor = uri_end;
    }
  }
  return {reinterpret_cast<const std::byte*>(text.data()),
          reinterpret_cast<const std::byte*>(text.data() + text.size())};
}

[[nodiscard]] bool process_asset(const source_node& node, std::vector<std::byte>& output,
                                 std::string& diagnostic) {
  if (node.processor->id == "gneiss.texture" && node.absolute_path.extension() == ".png") {
    return cook_png(node, output, diagnostic);
  }
  output = read_bytes(node.absolute_path);
  if (output.empty()) {
    diagnostic = "资产源文件为空或不可读";
    return false;
  }
  if (node.absolute_path.extension() == ".json") {
    output = rewrite_png_uris(std::move(output));
  }
  return true;
}

[[nodiscard]] bool write_bytes(const std::filesystem::path& target,
                               std::span<const std::byte> bytes) {
  std::error_code error;
  std::filesystem::create_directories(target.parent_path(), error);
  if (error) {
    return false;
  }
  std::ofstream stream(target, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return stream.good();
}

[[nodiscard]] bool read_texture_settings(const source_node& node, std::string& source,
                                         asset_internal::texture_transfer& transfer,
                                         bool& normal_map) {
  if (!std::string_view(node.relative_path).ends_with(".texture.json")) {
    return false;
  }
  const auto bytes = read_bytes(node.absolute_path);
  auto* document = yyjson_read(reinterpret_cast<const char*>(bytes.data()), bytes.size(), 0U);
  if (document == nullptr) {
    return false;
  }
  auto* root = yyjson_doc_get_root(document);
  auto* source_value = yyjson_obj_get(root, "source");
  auto* color_value = yyjson_obj_get(root, "color_space");
  auto* usage_value = yyjson_obj_get(root, "usage");
  auto* format_value = yyjson_obj_get(root, "format");
  auto* version_value = yyjson_obj_get(root, "version");
  const auto version = yyjson_get_uint(version_value);
  const std::string_view color = yyjson_is_str(color_value)
                                     ? std::string_view{yyjson_get_str(color_value)}
                                     : std::string_view{};
  normal_map =
      yyjson_is_str(usage_value) && std::string_view{yyjson_get_str(usage_value)} == "normal";
  const bool valid = yyjson_is_str(source_value) && yyjson_is_str(format_value) &&
                     std::string_view{yyjson_get_str(format_value)} == "gneiss.texture" &&
                     yyjson_is_uint(version_value) && (version == 1U || version == 2U) &&
                     (color == "linear" || color == "srgb") &&
                     ((version == 1U && usage_value == nullptr) ||
                      (version == 2U && normal_map && color == "linear"));
  if (valid) {
    const std::string_view uri{yyjson_get_str(source_value), yyjson_get_len(source_value)};
    constexpr std::string_view prefix = "asset://";
    if (uri.starts_with(prefix)) {
      source.assign(uri.substr(prefix.size()));
      transfer = color == "linear" ? asset_internal::texture_transfer::linear
                                   : asset_internal::texture_transfer::srgb;
    }
  }
  yyjson_doc_free(document);
  return valid && !source.empty();
}

[[nodiscard]] std::filesystem::path temporary_path(const std::filesystem::path& target,
                                                   std::string_view suffix) {
  auto output = target;
  output += suffix;
  output += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return output;
}

} // namespace

bool processor_registry::register_processor(processor_description processor) {
  if (processor.id.empty() || processor.version == 0U || processor.suffixes.empty() ||
      std::ranges::any_of(processor.suffixes, [](const auto& suffix) { return suffix.empty(); })) {
    return false;
  }
  for (const auto& existing : processors_) {
    if (existing.id == processor.id) {
      return false;
    }
    for (const auto& suffix : processor.suffixes) {
      if (std::ranges::find(existing.suffixes, suffix) != existing.suffixes.end()) {
        return false;
      }
    }
  }
  processors_.push_back(std::move(processor));
  return true;
}

const processor_description* processor_registry::find(std::string_view path) const noexcept {
  const processor_description* selected = nullptr;
  std::size_t selected_length{};
  for (const auto& processor : processors_) {
    for (const auto& suffix : processor.suffixes) {
      if (path.ends_with(suffix) && suffix.size() > selected_length) {
        selected = &processor;
        selected_length = suffix.size();
      }
    }
  }
  return selected;
}

processor_registry make_default_registry() {
  processor_registry registry;
  static_cast<void>(registry.register_processor(
      {.id = "gneiss.texture", .version = 5U, .suffixes = {".png", ".jpg", ".jpeg", ".ktx2"}}));
  static_cast<void>(registry.register_processor(
      {.id = "gneiss.mesh", .version = 2U, .suffixes = {".gneiss-mesh", ".mesh.json"}}));
  static_cast<void>(registry.register_processor(
      {.id = "gneiss.material", .version = 2U, .suffixes = {".material.json", ".grmat"}}));
  static_cast<void>(
      registry.register_processor({.id = "gneiss.document", .version = 2U, .suffixes = {".json"}}));
  static_cast<void>(
      registry.register_processor({.id = "gneiss.passthrough", .version = 1U, .suffixes = {"*"}}));
  return registry;
}

build_report build_assets(const build_request& request, const processor_registry& registry) {
  if (request.source_root.empty() || request.output_root.empty() || request.cache_root.empty() ||
      request.target_platform.empty() || request.target_architecture.empty()) {
    return fail(build_result::invalid_argument, "资产构建参数不完整");
  }
  try {
    std::error_code error;
    if (!std::filesystem::is_directory(request.source_root, error) || error) {
      return fail(build_result::source_unavailable, "资产源目录不存在");
    }
    if (std::filesystem::exists(request.output_root, error) || error) {
      return fail(build_result::output_exists, "资产构建输出已存在");
    }
    std::map<std::string, source_node> nodes;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(request.source_root, error)) {
      if (error) {
        return fail(build_result::io_error, "扫描资产源目录失败");
      }
      if (!entry.is_regular_file(error) || error) {
        if (error) {
          return fail(build_result::io_error, "读取资产源文件状态失败");
        }
        continue;
      }
      const auto relative = path_utf8(entry.path().lexically_relative(request.source_root));
      if (!is_runtime_source(relative)) {
        continue;
      }
      const auto* processor = registry.find(relative);
      if (processor == nullptr) {
        processor = registry.find("*");
      }
      if (processor == nullptr) {
        return fail(build_result::processor_missing, "资产没有可用处理器：" + relative);
      }
      source_node node{.absolute_path = entry.path(),
                       .relative_path = relative,
                       .processor = processor,
                       .dependencies = scan_dependencies(entry.path()),
                       .cache_key = {},
                       .texture_transfer = {}};
      nodes.emplace(relative, std::move(node));
    }

    for (const auto& [relative, node] : nodes) {
      static_cast<void>(relative);
      std::string image_source;
      asset_internal::texture_transfer transfer{};
      bool normal_map{};
      if (!read_texture_settings(node, image_source, transfer, normal_map)) {
        if (std::string_view(node.relative_path).ends_with(".texture.json")) {
          return fail(build_result::processor_failed,
                      "Texture 描述或用途无效：" + node.relative_path);
        }
        continue;
      }
      const auto image = nodes.find(image_source);
      if (image == nodes.end() || image->second.processor->id != "gneiss.texture") {
        continue;
      }
      if (image->second.texture_transfer && *image->second.texture_transfer != transfer) {
        return fail(build_result::processor_failed,
                    "同一源图像不能同时使用 linear 与 srgb：" + image_source);
      }
      if (image->second.normal_map && *image->second.normal_map != normal_map) {
        return fail(build_result::processor_failed,
                    "同一派生图像不能混用法线和颜色/数据处理，请生成独立源路径：" + image_source);
      }
      image->second.texture_transfer = transfer;
      image->second.normal_map = normal_map;
    }

    std::set<std::string> selected;
    if (request.profile == build_profile::development) {
      for (const auto& [relative, node] : nodes) {
        static_cast<void>(node);
        selected.insert(relative);
      }
    } else {
      std::queue<std::string> pending;
      for (const auto& uri : request.root_uris) {
        constexpr std::string_view prefix = "asset://";
        if (!std::string_view(uri).starts_with(prefix) ||
            !safe_relative_path(std::string_view(uri).substr(prefix.size()))) {
          return fail(build_result::invalid_argument, "Shipping 根资产 URI 无效：" + uri);
        }
        pending.emplace(std::string_view(uri).substr(prefix.size()));
      }
      while (!pending.empty()) {
        auto relative = std::move(pending.front());
        pending.pop();
        if (!selected.insert(relative).second) {
          continue;
        }
        const auto node = nodes.find(relative);
        if (node == nodes.end()) {
          return fail(build_result::dependency_missing, "缺少可达资产：" + relative);
        }
        for (const auto& dependency : node->second.dependencies) {
          pending.push(dependency);
        }
      }
    }

    for (const auto& relative : selected) {
      auto& node = nodes.at(relative);
      if (!validate_source(node)) {
        return fail(build_result::processor_failed, "资产处理器校验失败：" + relative);
      }
      if (!cache_key(node, request, node.cache_key)) {
        return fail(build_result::io_error, "无法计算资产缓存键：" + relative);
      }
    }

    std::map<std::string, std::uint8_t> visit_state;
    std::function<bool(source_node&)> finalize_key = [&](source_node& node) {
      auto& state = visit_state[node.relative_path];
      if (state == 2U) {
        return true;
      }
      if (state == 1U) {
        return false;
      }
      state = 1U;
      std::uint64_t dependency_hash = 14695981039346656037ULL;
      hash_bytes(dependency_hash, node.cache_key);
      for (const auto& dependency : node.dependencies) {
        const auto found = nodes.find(dependency);
        if (found == nodes.end() || !finalize_key(found->second)) {
          return false;
        }
        hash_bytes(dependency_hash, "\0");
        hash_bytes(dependency_hash, dependency);
        hash_bytes(dependency_hash, "\0");
        hash_bytes(dependency_hash, found->second.cache_key);
      }
      std::ostringstream text;
      text << std::hex << std::setfill('0') << std::setw(16) << dependency_hash;
      node.cache_key = text.str();
      state = 2U;
      return true;
    };
    for (const auto& relative : selected) {
      auto& node = nodes.at(relative);
      if (!finalize_key(node)) {
        return fail(build_result::dependency_missing, "资产依赖缺失或形成循环：" + relative);
      }
    }

    const auto temporary = temporary_path(request.output_root, ".gneiss-building-");
    std::filesystem::create_directories(temporary, error);
    if (error) {
      return fail(build_result::io_error, "无法创建资产构建暂存目录");
    }
    build_report report{.result = build_result::success,
                        .source_count = static_cast<std::uint64_t>(nodes.size()),
                        .built_count = 0U,
                        .cache_hit_count = 0U,
                        .pruned_count = static_cast<std::uint64_t>(nodes.size() - selected.size()),
                        .outputs = {},
                        .diagnostic = {}};
    std::uint64_t progress_index{};
    std::set<std::string> output_paths;
    for (const auto& relative : selected) {
      const auto& node = nodes.at(relative);
      const auto output_relative = output_relative_path(node);
      if (!output_paths.insert(output_relative).second) {
        std::filesystem::remove_all(temporary, error);
        return fail(build_result::processor_failed, "多个资产生成相同输出：" + output_relative);
      }
      const auto cache_file = request.cache_root / request.target_platform /
                              request.target_architecture / node.cache_key / "payload";
      const auto output_file = temporary / utf8_path(output_relative);
      bool was_cache_hit = false;
      if (std::filesystem::is_regular_file(cache_file, error) && !error) {
        if (!copy_asset_file(cache_file, output_file)) {
          std::filesystem::remove_all(temporary, error);
          return fail(build_result::io_error, "无法读取资产缓存：" + relative);
        }
        ++report.cache_hit_count;
        was_cache_hit = true;
      } else {
        error.clear();
        const auto cache_temporary = temporary_path(cache_file, ".gneiss-writing-");
        std::vector<std::byte> processed;
        std::string processor_diagnostic;
        if (!process_asset(node, processed, processor_diagnostic) ||
            !write_bytes(cache_temporary, processed)) {
          std::filesystem::remove_all(temporary, error);
          std::filesystem::remove(cache_temporary, error);
          return fail(build_result::processor_failed,
                      "资产处理器写入失败：" + relative +
                          (processor_diagnostic.empty() ? std::string{}
                                                        : "（" + processor_diagnostic + "）"));
        }
        std::filesystem::create_directories(cache_file.parent_path(), error);
        if (!error) {
          std::filesystem::rename(cache_temporary, cache_file, error);
        }
        if (error && !std::filesystem::is_regular_file(cache_file)) {
          std::filesystem::remove_all(temporary, error);
          std::filesystem::remove(cache_temporary, error);
          return fail(build_result::io_error, "无法提交资产缓存：" + relative);
        }
        std::filesystem::remove(cache_temporary, error);
        error.clear();
        if (!copy_asset_file(cache_file, output_file)) {
          std::filesystem::remove_all(temporary, error);
          return fail(build_result::io_error, "无法提交资产构建输出：" + relative);
        }
        ++report.built_count;
      }
      auto output_dependencies = node.dependencies;
      for (auto& dependency : output_dependencies) {
        const auto found = nodes.find(dependency);
        if (found != nodes.end()) {
          dependency = output_relative_path(found->second);
        }
      }
      report.outputs.push_back(
          {output_relative, node.processor->id, node.cache_key, std::move(output_dependencies)});
      ++progress_index;
      if (request.progress) {
        request.progress({.current = progress_index,
                          .total = static_cast<std::uint64_t>(selected.size()),
                          .relative_path = output_relative,
                          .cache_hit = was_cache_hit});
      }
    }
    std::filesystem::rename(temporary, request.output_root, error);
    if (error) {
      std::filesystem::remove_all(temporary, error);
      return fail(build_result::io_error, "无法提交资产构建目录");
    }
    return report;
  } catch (...) {
    return fail(build_result::io_error, "资产构建发生未预期的文件系统错误");
  }
}

} // namespace gneiss::tooling::asset_build
