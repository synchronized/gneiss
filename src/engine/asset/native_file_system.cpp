// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/native_file_system.hpp"

#include "engine/asset/asset_uri.hpp"
#include "engine/core/diagnostics/profiling.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <system_error>
#include <vector>

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
struct native_async_file {
  explicit native_async_file(HANDLE value) : handle(value) {}
  ~native_async_file() { (void)CloseHandle(handle); }
  native_async_file(const native_async_file&) = delete;
  native_async_file& operator=(const native_async_file&) = delete;
  HANDLE handle;
};

class native_read_operation final : public gneiss::asset_internal::read_operation {
public:
  native_read_operation(std::shared_ptr<native_async_file> file, std::size_t size)
      : file_(std::move(file)), bytes_(size) {}
  ~native_read_operation() override {
    GNEISS_PROFILE_SCOPE("asset.io.request.destroy");
    if (pending_) {
      (void)CancelIoEx(file_->handle, &overlapped_);
      DWORD ignored{};
      // 缓冲和 OVERLAPPED 必须存活到内核确认完成/取消，不把悬空内存交给 I/O。
      (void)GetOverlappedResult(file_->handle, &overlapped_, &ignored, TRUE);
    }
    if (overlapped_.hEvent != nullptr) {
      (void)CloseHandle(overlapped_.hEvent);
    }
  }
  gneiss_result start(std::uint64_t offset) noexcept {
    GNEISS_PROFILE_SCOPE("asset.io.request.start");
    if (bytes_.empty()) {
      return GNEISS_SUCCESS;
    }
    overlapped_.Offset = static_cast<DWORD>(offset);
    overlapped_.OffsetHigh = static_cast<DWORD>(offset >> 32U);
    overlapped_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped_.hEvent == nullptr) {
      return GNEISS_ERROR_IO;
    }
    DWORD count{};
    if (ReadFile(file_->handle, bytes_.data(), static_cast<DWORD>(bytes_.size()), &count,
                 &overlapped_) != FALSE) {
      return count == bytes_.size() ? GNEISS_SUCCESS : GNEISS_ERROR_IO;
    }
    if (GetLastError() != ERROR_IO_PENDING) {
      return GNEISS_ERROR_IO;
    }
    pending_ = true;
    return GNEISS_SUCCESS;
  }
  [[nodiscard]] gneiss_result poll(std::span<const std::byte>& output) noexcept override {
    output = {};
    if (pending_) {
      DWORD count{};
      const auto completed = GetOverlappedResult(file_->handle, &overlapped_, &count, FALSE);
      if (completed == FALSE && GetLastError() == ERROR_IO_INCOMPLETE) {
        return GNEISS_ERROR_NOT_READY;
      }
      pending_ = false;
      result_ = completed != FALSE && count == bytes_.size() ? GNEISS_SUCCESS : GNEISS_ERROR_IO;
    }
    if (result_ == GNEISS_SUCCESS) {
      output = bytes_;
    }
    return result_;
  }
  [[nodiscard]] gneiss_result wait_for(std::chrono::milliseconds timeout,
                                       std::span<const std::byte>& output) noexcept override {
    output = {};
    if (timeout.count() < 0 || static_cast<std::uint64_t>(timeout.count()) >= INFINITE) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (pending_ && timeout.count() != 0) {
      const auto waited =
          WaitForSingleObject(overlapped_.hEvent, static_cast<DWORD>(timeout.count()));
      if (waited == WAIT_TIMEOUT) {
        return GNEISS_ERROR_NOT_READY;
      }
      if (waited != WAIT_OBJECT_0) {
        return GNEISS_ERROR_IO;
      }
    }
    return poll(output);
  }

private:
  std::shared_ptr<native_async_file> file_;
  std::vector<std::byte> bytes_;
  OVERLAPPED overlapped_{};
  bool pending_{};
  gneiss_result result_{GNEISS_SUCCESS};
};

class native_read_source final : public gneiss::asset_internal::read_source {
public:
  ~native_read_source() override {
    if (handle_ != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(handle_);
    }
  }
  gneiss_result open(const std::filesystem::path& path) noexcept {
    GNEISS_PROFILE_SCOPE("asset.io.open");
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
  [[nodiscard]] gneiss_result begin_read(
      std::uint64_t offset, std::size_t size,
      std::unique_ptr<gneiss::asset_internal::read_operation>& output) const noexcept override {
    GNEISS_PROFILE_SCOPE("asset.io.begin_read");
    output.reset();
    if (offset > length_ || size > length_ - offset || size > std::numeric_limits<DWORD>::max()) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    try {
      std::shared_ptr<native_async_file> file;
      {
        const std::scoped_lock lock(async_mutex_);
        if (!async_file_) {
          GNEISS_PROFILE_SCOPE("asset.io.reopen");
          auto* const reopened = ReOpenFile(handle_, GENERIC_READ,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                            FILE_FLAG_OVERLAPPED);
          if (reopened == INVALID_HANDLE_VALUE) {
            return GNEISS_ERROR_IO;
          }
          try {
            async_file_ = std::make_shared<native_async_file>(reopened);
          } catch (...) {
            (void)CloseHandle(reopened);
            throw;
          }
        }
        file = async_file_;
      }
      std::unique_ptr<native_read_operation> request;
      {
        GNEISS_PROFILE_SCOPE("asset.io.request.allocate");
        request = std::make_unique<native_read_operation>(std::move(file), size);
      }
      const auto started = request->start(offset);
      if (started == GNEISS_SUCCESS) {
        output = std::move(request);
      }
      return started;
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }
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
  mutable std::shared_ptr<native_async_file> async_file_;
  mutable std::mutex async_mutex_;
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
