// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/native_file_system.hpp"

#include "engine/asset/asset_uri.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <mutex>
#include <new>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
class native_read_source final : public gneiss::asset_internal::read_source {
public:
  ~native_read_source() override {
    if (handle_ != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(handle_);
    }
  }
  gneiss_result open(const std::filesystem::path& path) noexcept {
    handle_ = CreateFileW(path.c_str(), GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    LARGE_INTEGER length{};
    if (handle_ == INVALID_HANDLE_VALUE || GetFileSizeEx(handle_, &length) == FALSE ||
        length.QuadPart < 0) {
      return GNEISS_ERROR_IO;
    }
    length_ = static_cast<std::uint64_t>(length.QuadPart);
    return GNEISS_SUCCESS;
  }
  std::uint64_t size() const noexcept override { return length_; }
  gneiss_result read_at(std::uint64_t offset, std::span<std::byte> output) const noexcept override {
    if (offset > length_ || output.size() > length_ - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (output.empty()) {
      return GNEISS_SUCCESS;
    }
    try {
      const std::scoped_lock lock(mutex_);
      LARGE_INTEGER position{};
      position.QuadPart = static_cast<LONGLONG>(offset);
      if (SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN) == FALSE) {
        return GNEISS_ERROR_IO;
      }
      while (!output.empty()) {
        const auto chunk = static_cast<DWORD>(
            std::min<std::size_t>(output.size(), std::numeric_limits<DWORD>::max()));
        DWORD count{};
        if (ReadFile(handle_, output.data(), chunk, &count, nullptr) == FALSE || count != chunk) {
          return GNEISS_ERROR_IO;
        }
        output = output.subspan(count);
      }
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_IO;
    }
  }

private:
  HANDLE handle_{INVALID_HANDLE_VALUE};
  std::uint64_t length_{};
  mutable std::mutex mutex_;
};
#else
class native_read_source final : public gneiss::asset_internal::read_source {
public:
  native_read_source(std::ifstream stream, std::uint64_t length)
      : stream_(std::move(stream)), length_(length) {}

  std::uint64_t size() const noexcept override { return length_; }

  gneiss_result read_at(std::uint64_t offset, std::span<std::byte> output) const noexcept override {
    if (offset > length_ || output.size() > length_ - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        output.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (output.empty()) {
      return GNEISS_SUCCESS;
    }
    try {
      const std::scoped_lock lock(mutex_);
      stream_.clear();
      stream_.seekg(static_cast<std::streamoff>(offset));
      if (!stream_.read(reinterpret_cast<char*>(output.data()),
                        static_cast<std::streamsize>(output.size()))) {
        return GNEISS_ERROR_IO;
      }
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_IO;
    }
  }

private:
  mutable std::ifstream stream_;
  const std::uint64_t length_;
  mutable std::mutex mutex_;
};

#endif

[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view text) {
  std::u8string value;
  value.reserve(text.size());
  for (const char character : text) {
    value.push_back(static_cast<char8_t>(character));
  }
  return {value};
}

} // namespace

namespace gneiss::asset_internal {

gneiss_result native_file_system::open_read(std::string_view path,
                                            std::unique_ptr<read_source>& output) const noexcept {
  output.reset();
  if (root_.empty()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  try {
    if (validate_uri("asset://" + std::string(path)) != GNEISS_SUCCESS) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::error_code error;
    const auto candidate = std::filesystem::canonical(root_ / path_from_utf8(path), error);
    if (error || !std::filesystem::is_regular_file(candidate, error) || error) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    const auto relative = std::filesystem::relative(candidate, root_, error);
    if (error || relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
#ifdef _WIN32
    auto source = std::make_unique<native_read_source>();
    const auto opened = source->open(candidate);
    if (opened != GNEISS_SUCCESS) {
      return opened;
    }
    output = std::move(source);
#else
    std::ifstream stream(candidate, std::ios::binary);
    if (!stream) {
      return GNEISS_ERROR_IO;
    }
    // 从已打开对象获取长度，避免另一次路径查询指向替换后的文件。
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < std::streampos(0)) {
      return GNEISS_ERROR_IO;
    }
    output =
        std::make_unique<native_read_source>(std::move(stream), static_cast<std::uint64_t>(end));
#endif
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result native_file_system::initialize(std::string_view root) noexcept {
  try {
    const auto path = path_from_utf8(root);
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    root_ = canonical;
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result native_file_system::read(std::string_view path,
                                       std::vector<std::byte>& out_bytes) const noexcept {
  return read_bounded(path, std::numeric_limits<std::size_t>::max(), out_bytes);
}

gneiss_result native_file_system::read_bounded(std::string_view path, std::size_t limit,
                                               std::vector<std::byte>& out_bytes) const noexcept {
  if (root_.empty()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  try {
    std::string uri = "asset://";
    uri.append(path);
    if (validate_uri(uri) != GNEISS_SUCCESS) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }

    std::error_code error;
    const auto candidate = std::filesystem::canonical(root_ / path_from_utf8(path), error);
    if (error || !std::filesystem::is_regular_file(candidate, error) || error) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    const auto relative = std::filesystem::relative(candidate, root_, error);
    if (error || relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::ifstream stream(candidate, std::ios::binary);
    if (!stream) {
      return GNEISS_ERROR_IO;
    }
    const auto size = std::filesystem::file_size(candidate, error);
    if (error) {
      return GNEISS_ERROR_IO;
    }
    if (size > limit ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)) ||
        stream.peek() != std::char_traits<char>::eof()) {
      return GNEISS_ERROR_IO;
    }
    out_bytes = std::move(bytes);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::asset_internal
