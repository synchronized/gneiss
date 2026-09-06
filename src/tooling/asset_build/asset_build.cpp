// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/asset_build.h"

#include "tooling/asset_build/ktx2_probe.h"

#include "asset/mesh_binary.h"

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <map>
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
    return bytes.size() >= 2U && bytes[0] == std::byte{0xFFU} && bytes[1] == std::byte{0xD8U};
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
  std::ostringstream text;
  text << std::hex << std::setfill('0') << std::setw(16) << hash;
  output = text.str();
  return true;
}

[[nodiscard]] build_report fail(build_result result, std::string diagnostic) {
  return {.result = result, .diagnostic = std::move(diagnostic)};
}

[[nodiscard]] bool copy_asset_file(const std::filesystem::path& source,
                                   const std::filesystem::path& target) {
  std::error_code error;
  std::filesystem::create_directories(target.parent_path(), error);
  return !error &&
         std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, error) &&
         !error;
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
      {.id = "gneiss.texture", .version = 1U, .suffixes = {".png", ".jpg", ".jpeg", ".ktx2"}}));
  static_cast<void>(registry.register_processor(
      {.id = "gneiss.mesh", .version = 1U, .suffixes = {".gneiss-mesh", ".mesh.json"}}));
  static_cast<void>(registry.register_processor(
      {.id = "gneiss.material", .version = 1U, .suffixes = {".material.json", ".grmat"}}));
  static_cast<void>(
      registry.register_processor({.id = "gneiss.document", .version = 1U, .suffixes = {".json"}}));
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
                       .dependencies = scan_dependencies(entry.path())};
      nodes.emplace(relative, std::move(node));
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
                        .pruned_count = static_cast<std::uint64_t>(nodes.size() - selected.size())};
    std::uint64_t progress_index{};
    for (const auto& relative : selected) {
      const auto& node = nodes.at(relative);
      const auto cache_file = request.cache_root / request.target_platform /
                              request.target_architecture / node.cache_key / "payload";
      const auto output_file = temporary / utf8_path(relative);
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
        if (!copy_asset_file(node.absolute_path, cache_temporary)) {
          std::filesystem::remove_all(temporary, error);
          std::filesystem::remove(cache_temporary, error);
          return fail(build_result::io_error, "资产处理器写入失败：" + relative);
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
      report.outputs.push_back({relative, node.processor->id, node.cache_key, node.dependencies});
      ++progress_index;
      if (request.progress) {
        request.progress({.current = progress_index,
                          .total = static_cast<std::uint64_t>(selected.size()),
                          .relative_path = relative,
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
