// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::tooling::asset_build {

enum class build_profile : std::uint8_t { development, shipping };

enum class build_result : std::uint8_t {
  success,
  invalid_argument,
  source_unavailable,
  dependency_missing,
  processor_missing,
  output_exists,
  io_error,
};

struct processor_description final {
  std::string id;
  std::uint32_t version{};
  std::vector<std::string> suffixes;
};

class processor_registry final {
public:
  /** 注册一个处理器；标识和后缀不得与现有处理器重复。 */
  [[nodiscard]] bool register_processor(processor_description processor);

  /** 按最长文件后缀查找处理器，找不到时返回空指针。 */
  [[nodiscard]] const processor_description* find(std::string_view path) const noexcept;

  [[nodiscard]] const std::vector<processor_description>& processors() const noexcept {
    return processors_;
  }

private:
  std::vector<processor_description> processors_;
};

/** 返回 Texture、Mesh、Material、JSON 与普通文件的首批内建处理器。 */
[[nodiscard]] processor_registry make_default_registry();

struct build_request final {
  std::filesystem::path source_root;
  std::filesystem::path output_root;
  std::filesystem::path cache_root;
  std::vector<std::string> root_uris;
  std::string target_platform;
  std::string target_architecture;
  build_profile profile{build_profile::development};
};

struct build_output final {
  std::string relative_path;
  std::string processor_id;
  std::string cache_key;
  std::vector<std::string> dependencies;
};

struct build_report final {
  build_result result{build_result::invalid_argument};
  std::uint64_t source_count{};
  std::uint64_t built_count{};
  std::uint64_t cache_hit_count{};
  std::uint64_t pruned_count{};
  std::vector<build_output> outputs;
  std::string diagnostic;
};

/**
 * 构建运行资产并事务式提交 output_root。
 *
 * Development 构建全部源资产；Shipping 只构建 root_uris 可达的资产。缓存写入使用临时文件提交，
 * 调用者不得同时修改 source_root。函数不覆盖既有 output_root。
 */
[[nodiscard]] build_report build_assets(const build_request& request,
                                        const processor_registry& registry);

} // namespace gneiss::tooling::asset_build
