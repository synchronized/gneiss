// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "package_archive.hpp"

#include <gneiss/engine/core/version.h>

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace gneiss::editor {
namespace {

struct package_file final {
  std::filesystem::path absolute;
  std::string relative;
  std::uint64_t size{};
};

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::filesystem::path utf8_path(std::string_view text) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

[[nodiscard]] result collect_files(const std::filesystem::path& root,
                                   std::vector<package_file>& output) {
  std::error_code error;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
    if (error) {
      return result::io;
    }
    if (!entry.is_regular_file(error) || error) {
      if (error) {
        return result::io;
      }
      continue;
    }
    const auto relative = entry.path().lexically_relative(root);
    const auto text = path_utf8(relative);
    if (relative.empty() || relative.is_absolute() || text.starts_with("../") ||
        text.find("/../") != std::string::npos) {
      return result::invalid_argument;
    }
    const auto size = entry.file_size(error);
    if (error) {
      return result::io;
    }
    output.push_back({entry.path(), text, size});
  }
  std::ranges::sort(output, {}, &package_file::relative);
  return result::success;
}

constexpr std::array<std::uint32_t, 64U> sha256_constants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

class sha256 final {
public:
  void update(const std::uint8_t* data, std::size_t size) noexcept {
    total_size_ += size;
    while (size > 0U) {
      const auto count = std::min(size, block_.size() - block_size_);
      std::copy_n(data, count, block_.data() + block_size_);
      data += count;
      size -= count;
      block_size_ += count;
      if (block_size_ == block_.size()) {
        transform();
        block_size_ = 0U;
      }
    }
  }

  [[nodiscard]] std::array<std::uint8_t, 32U> finish() noexcept {
    const auto bit_size = static_cast<std::uint64_t>(total_size_) * 8U;
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56U) {
      std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.end(),
                std::uint8_t{0});
      transform();
      block_size_ = 0U;
    }
    std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.begin() + 56,
              std::uint8_t{0});
    for (std::size_t index = 0U; index < 8U; ++index) {
      block_[63U - index] = static_cast<std::uint8_t>(bit_size >> (index * 8U));
    }
    transform();
    std::array<std::uint8_t, 32U> output{};
    for (std::size_t index = 0U; index < state_.size(); ++index) {
      for (std::size_t byte = 0U; byte < 4U; ++byte) {
        output[(index * 4U) + byte] =
            static_cast<std::uint8_t>(state_[index] >> ((3U - byte) * 8U));
      }
    }
    return output;
  }

private:
  void transform() noexcept {
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
      const auto offset = index * 4U;
      words[index] = (static_cast<std::uint32_t>(block_[offset]) << 24U) |
                     (static_cast<std::uint32_t>(block_[offset + 1U]) << 16U) |
                     (static_cast<std::uint32_t>(block_[offset + 2U]) << 8U) |
                     static_cast<std::uint32_t>(block_[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
      const auto a = words[index - 15U];
      const auto b = words[index - 2U];
      const auto s0 = std::rotr(a, 7) ^ std::rotr(a, 18) ^ (a >> 3U);
      const auto s1 = std::rotr(b, 17) ^ std::rotr(b, 19) ^ (b >> 10U);
      words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }
    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];
    for (std::size_t index = 0U; index < words.size(); ++index) {
      const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const auto choose = (e & f) ^ (~e & g);
      const auto temporary1 = h + sum1 + choose + sha256_constants[index] + words[index];
      const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8U> state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                          0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<std::uint8_t, 64U> block_{};
  std::size_t block_size_{};
  std::size_t total_size_{};
};

[[nodiscard]] result file_sha256(const std::filesystem::path& path, std::string& output) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return result::io;
  }
  sha256 hash;
  std::array<std::uint8_t, 64U * 1024U> buffer{};
  while (stream) {
    stream.read(reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(buffer.size()));
    hash.update(buffer.data(), static_cast<std::size_t>(stream.gcount()));
  }
  if (!stream.eof()) {
    return result::io;
  }
  std::ostringstream text;
  text << std::hex << std::setfill('0');
  for (const auto byte : hash.finish()) {
    text << std::setw(2) << static_cast<unsigned int>(byte);
  }
  output = text.str();
  return result::success;
}

[[nodiscard]] constexpr std::string_view platform_name() noexcept {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

[[nodiscard]] constexpr std::string_view architecture_name() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
  return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#else
  return "unknown";
#endif
}

void write_u16(std::ostream& stream, std::uint16_t value) {
  const std::array bytes = {static_cast<char>(value), static_cast<char>(value >> 8U)};
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u32(std::ostream& stream, std::uint32_t value) {
  const std::array bytes = {static_cast<char>(value), static_cast<char>(value >> 8U),
                            static_cast<char>(value >> 16U), static_cast<char>(value >> 24U)};
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] std::uint32_t crc32_update(std::uint32_t crc, const std::uint8_t* data,
                                         std::size_t size) noexcept {
  for (std::size_t index = 0U; index < size; ++index) {
    crc ^= data[index];
    for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
      crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
  }
  return crc;
}

struct zip_entry final {
  package_file file;
  std::uint32_t crc{};
  std::uint32_t offset{};
};

} // namespace

result write_package_manifest(const std::filesystem::path& package_root,
                              const app::project_description& project,
                              app::game_build_profile profile,
                              std::string_view entrypoint) noexcept {
  try {
    std::vector<package_file> files;
    auto operation = collect_files(package_root, files);
    if (!operation) {
      return operation;
    }
    std::erase_if(files, [](const auto& file) { return file.relative == "gneiss.package.json"; });
    using document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
    document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    if (!document) {
      return result::out_of_memory;
    }
    auto* root = yyjson_mut_obj(document.get());
    auto* entries = yyjson_mut_arr(document.get());
    const auto profile_name = app::game_build_profile_name(profile);
    if (root == nullptr || entries == nullptr ||
        !yyjson_mut_obj_add_str(document.get(), root, "format", "gneiss.package") ||
        !yyjson_mut_obj_add_uint(document.get(), root, "version", 1U) ||
        !yyjson_mut_obj_add_str(document.get(), root, "gneiss_version", GNEISS_VERSION_STRING) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "project", project.name.data(),
                                    project.name.size()) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "profile", profile_name.data(),
                                    profile_name.size()) ||
        !yyjson_mut_obj_add_str(document.get(), root, "platform", platform_name().data()) ||
        !yyjson_mut_obj_add_str(document.get(), root, "architecture", architecture_name().data()) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "entrypoint", entrypoint.data(),
                                    entrypoint.size())) {
      return result::out_of_memory;
    }
    for (const auto& file : files) {
      std::string hash;
      operation = file_sha256(file.absolute, hash);
      auto* entry = yyjson_mut_obj(document.get());
      if (!operation || entry == nullptr ||
          !yyjson_mut_obj_add_strncpy(document.get(), entry, "path", file.relative.data(),
                                      file.relative.size()) ||
          !yyjson_mut_obj_add_uint(document.get(), entry, "size", file.size) ||
          !yyjson_mut_obj_add_strncpy(document.get(), entry, "sha256", hash.data(), hash.size()) ||
          !yyjson_mut_arr_add_val(entries, entry)) {
        return operation ? result::out_of_memory : operation;
      }
    }
    if (!yyjson_mut_obj_add_val(document.get(), root, "files", entries)) {
      return result::out_of_memory;
    }
    yyjson_mut_doc_set_root(document.get(), root);
    std::size_t size = 0U;
    std::unique_ptr<char, decltype(&std::free)> json(
        yyjson_mut_write(document.get(), YYJSON_WRITE_PRETTY | YYJSON_WRITE_NEWLINE_AT_END, &size),
        &std::free);
    std::ofstream stream(package_root / "gneiss.package.json", std::ios::binary | std::ios::trunc);
    if (!json || !stream.write(json.get(), static_cast<std::streamsize>(size))) {
      return result::io;
    }
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result write_deterministic_zip(const std::filesystem::path& package_root,
                               const std::filesystem::path& archive) noexcept {
  try {
    std::vector<package_file> files;
    auto operation = collect_files(package_root, files);
    if (!operation || files.size() > std::numeric_limits<std::uint16_t>::max()) {
      return operation ? result::unsupported : operation;
    }
    std::vector<zip_entry> entries;
    entries.reserve(files.size());
    std::ofstream output(archive, std::ios::binary | std::ios::trunc);
    std::array<std::uint8_t, 64U * 1024U> buffer{};
    for (auto& file : files) {
      if (file.size > std::numeric_limits<std::uint32_t>::max() ||
          file.relative.size() > std::numeric_limits<std::uint16_t>::max()) {
        return result::unsupported;
      }
      std::ifstream input(file.absolute, std::ios::binary);
      std::uint32_t crc = 0xffffffffU;
      while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        crc = crc32_update(crc, buffer.data(), static_cast<std::size_t>(input.gcount()));
      }
      if (!input.eof()) {
        return result::io;
      }
      crc ^= 0xffffffffU;
      const auto position = output.tellp();
      if (position < 0 ||
          static_cast<std::uint64_t>(position) > std::numeric_limits<std::uint32_t>::max()) {
        return result::unsupported;
      }
      write_u32(output, 0x04034b50U);
      write_u16(output, 20U);
      write_u16(output, 0x0800U);
      write_u16(output, 0U);
      write_u16(output, 0U);
      write_u16(output, 0x0021U);
      write_u32(output, crc);
      write_u32(output, static_cast<std::uint32_t>(file.size));
      write_u32(output, static_cast<std::uint32_t>(file.size));
      write_u16(output, static_cast<std::uint16_t>(file.relative.size()));
      write_u16(output, 0U);
      output.write(file.relative.data(), static_cast<std::streamsize>(file.relative.size()));
      input.clear();
      input.seekg(0);
      while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        output.write(reinterpret_cast<const char*>(buffer.data()), input.gcount());
      }
      entries.push_back({std::move(file), crc, static_cast<std::uint32_t>(position)});
    }
    const auto central_offset = output.tellp();
    for (const auto& entry : entries) {
      write_u32(output, 0x02014b50U);
      write_u16(output, 0x0314U);
      write_u16(output, 20U);
      write_u16(output, 0x0800U);
      write_u16(output, 0U);
      write_u16(output, 0U);
      write_u16(output, 0x0021U);
      write_u32(output, entry.crc);
      write_u32(output, static_cast<std::uint32_t>(entry.file.size));
      write_u32(output, static_cast<std::uint32_t>(entry.file.size));
      write_u16(output, static_cast<std::uint16_t>(entry.file.relative.size()));
      write_u16(output, 0U);
      write_u16(output, 0U);
      write_u16(output, 0U);
      write_u16(output, 0U);
      write_u32(output, entry.file.relative == "run.sh" ? 0100755U << 16U : 0100644U << 16U);
      write_u32(output, entry.offset);
      output.write(entry.file.relative.data(),
                   static_cast<std::streamsize>(entry.file.relative.size()));
    }
    const auto central_end = output.tellp();
    if (central_offset < 0 || central_end < central_offset ||
        static_cast<std::uint64_t>(central_end) > std::numeric_limits<std::uint32_t>::max()) {
      return result::unsupported;
    }
    write_u32(output, 0x06054b50U);
    write_u16(output, 0U);
    write_u16(output, 0U);
    write_u16(output, static_cast<std::uint16_t>(entries.size()));
    write_u16(output, static_cast<std::uint16_t>(entries.size()));
    write_u32(output, static_cast<std::uint32_t>(central_end - central_offset));
    write_u32(output, static_cast<std::uint32_t>(central_offset));
    write_u16(output, 0U);
    return output ? result::success : result::io;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result verify_package_manifest(const std::filesystem::path& package_root) noexcept {
  try {
    std::ifstream stream(package_root / "gneiss.package.json", std::ios::binary);
    const std::string json{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    if (!stream.is_open() || json.empty()) {
      return result::not_found;
    }
    using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    document_ptr document(yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    auto* format = yyjson_is_obj(root) ? yyjson_obj_get(root, "format") : nullptr;
    auto* version = yyjson_is_obj(root) ? yyjson_obj_get(root, "version") : nullptr;
    auto* entries = yyjson_is_obj(root) ? yyjson_obj_get(root, "files") : nullptr;
    if (!yyjson_is_str(format) ||
        std::string_view(yyjson_get_str(format), yyjson_get_len(format)) != "gneiss.package" ||
        !yyjson_is_uint(version) || yyjson_get_uint(version) != 1U || !yyjson_is_arr(entries)) {
      return result::invalid_argument;
    }
    std::vector<package_file> files;
    auto operation = collect_files(package_root, files);
    if (!operation) {
      return operation;
    }
    std::erase_if(files, [](const auto& file) { return file.relative == "gneiss.package.json"; });
    if (yyjson_arr_size(entries) != files.size()) {
      return result::dependency_failed;
    }
    std::size_t index = 0U;
    std::size_t maximum = 0U;
    yyjson_val* entry = nullptr;
    yyjson_arr_foreach(entries, index, maximum, entry) {
      auto* path = yyjson_is_obj(entry) ? yyjson_obj_get(entry, "path") : nullptr;
      auto* size = yyjson_is_obj(entry) ? yyjson_obj_get(entry, "size") : nullptr;
      auto* expected_hash = yyjson_is_obj(entry) ? yyjson_obj_get(entry, "sha256") : nullptr;
      if (!yyjson_is_str(path) || !yyjson_is_uint(size) || !yyjson_is_str(expected_hash)) {
        return result::invalid_argument;
      }
      const std::string_view relative(yyjson_get_str(path), yyjson_get_len(path));
      const std::string_view expected(yyjson_get_str(expected_hash), yyjson_get_len(expected_hash));
      if (relative != files[index].relative || yyjson_get_uint(size) != files[index].size ||
          utf8_path(relative).is_absolute() || relative.starts_with("../") ||
          relative.find("/../") != std::string_view::npos) {
        return result::dependency_failed;
      }
      std::string actual;
      operation = file_sha256(files[index].absolute, actual);
      if (!operation || actual != expected) {
        return operation ? result::dependency_failed : operation;
      }
    }
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

} // namespace gneiss::editor
