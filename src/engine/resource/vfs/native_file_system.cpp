// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/vfs/native_file_system.hpp"
#include "engine/core/result.hpp"

#include "engine/resource/vfs/asset_uri.hpp"
#ifdef GNEISS_ASSET_LIBUV
#include "engine/platform/io/uv/uv_async_file.hpp"
#include "engine/platform/io/uv/uv_file_reader.hpp"
#include "engine/platform/io/uv/uv_path_resolver.hpp"
#endif
#include "engine/core/diagnostics/profiling.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
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
#ifdef GNEISS_ENABLE_PROFILING
struct thread_execution_sample {
  ULONGLONG cpu_ticks{};
  ULONG64 cycles{};
  bool valid{};
};
thread_execution_sample sample_thread_execution() noexcept {
  FILETIME created{};
  FILETIME exited{};
  FILETIME kernel{};
  FILETIME user{};
  thread_execution_sample sample;
  sample.valid = GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != FALSE &&
                 QueryThreadCycleTime(GetCurrentThread(), &sample.cycles) != FALSE;
  if (sample.valid) {
    const auto ticks = [](FILETIME value) {
      return (static_cast<ULONGLONG>(value.dwHighDateTime) << 32U) | value.dwLowDateTime;
    };
    sample.cpu_ticks = ticks(kernel) + ticks(user);
  }
  return sample;
}
auto thread_execution_label(thread_execution_sample before,
                            thread_execution_sample after) noexcept {
  std::array<char, 160> text{};
  if (before.valid && after.valid) {
    std::snprintf(text.data(), text.size(), "os_tid=%lu cpu_100ns=%llu cycles=%llu",
                  static_cast<unsigned long>(GetCurrentThreadId()),
                  static_cast<unsigned long long>(after.cpu_ticks - before.cpu_ticks),
                  static_cast<unsigned long long>(after.cycles - before.cycles));
  } else {
    std::snprintf(text.data(), text.size(), "thread_counters_unavailable");
  }
  return text;
}
#endif
struct native_async_file {
  explicit native_async_file(HANDLE value) : handle(value) {}
  ~native_async_file() { (void)CloseHandle(handle); }
  native_async_file(const native_async_file&) = delete;
  native_async_file& operator=(const native_async_file&) = delete;
  HANDLE handle;
};

class native_read_operation final : public gneiss::engine::read_operation {
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
  gneiss::engine::result start(std::uint64_t offset) noexcept {
    GNEISS_PROFILE_SCOPE("asset.io.request.start");
    if (bytes_.empty()) {
      return gneiss::engine::result::success;
    }
    overlapped_.Offset = static_cast<DWORD>(offset);
    overlapped_.OffsetHigh = static_cast<DWORD>(offset >> 32U);
    overlapped_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped_.hEvent == nullptr) {
      return gneiss::engine::result::io;
    }
    DWORD count{};
    if (ReadFile(file_->handle, bytes_.data(), static_cast<DWORD>(bytes_.size()), &count,
                 &overlapped_) != FALSE) {
      return count == bytes_.size() ? gneiss::engine::result::success : gneiss::engine::result::io;
    }
    if (GetLastError() != ERROR_IO_PENDING) {
      return gneiss::engine::result::io;
    }
    pending_ = true;
    return gneiss::engine::result::success;
  }
  [[nodiscard]] gneiss::engine::result poll(std::span<const std::byte>& output) noexcept override {
    output = {};
    if (pending_) {
      DWORD count{};
      const auto completed = GetOverlappedResult(file_->handle, &overlapped_, &count, FALSE);
      if (completed == FALSE && GetLastError() == ERROR_IO_INCOMPLETE) {
        return gneiss::engine::result::not_ready;
      }
      pending_ = false;
      result_ = completed != FALSE && count == bytes_.size() ? gneiss::engine::result::success
                                                             : gneiss::engine::result::io;
    }
    if (result_ == gneiss::engine::result::success) {
      output = bytes_;
    }
    return result_;
  }
  [[nodiscard]] gneiss::engine::result
  wait_for(std::chrono::milliseconds timeout,
           std::span<const std::byte>& output) noexcept override {
    output = {};
    if (timeout.count() < 0 || static_cast<std::uint64_t>(timeout.count()) >= INFINITE) {
      return gneiss::engine::result::invalid_argument;
    }
    if (pending_ && timeout.count() != 0) {
      const auto waited =
          WaitForSingleObject(overlapped_.hEvent, static_cast<DWORD>(timeout.count()));
      if (waited == WAIT_TIMEOUT) {
        return gneiss::engine::result::not_ready;
      }
      if (waited != WAIT_OBJECT_0) {
        return gneiss::engine::result::io;
      }
    }
    return poll(output);
  }

private:
  std::shared_ptr<native_async_file> file_;
  std::vector<std::byte> bytes_;
  OVERLAPPED overlapped_{};
  bool pending_{};
  gneiss::engine::result result_{gneiss::engine::result::success};
};

class native_read_source final : public gneiss::engine::read_source {
public:
  ~native_read_source() override {
    if (handle_ != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(handle_);
    }
  }
  gneiss::engine::result open(const std::filesystem::path& path) noexcept {
    GNEISS_PROFILE_SCOPE("asset.io.open");
    {
      GNEISS_PROFILE_SCOPE("asset.io.create_file");
#ifdef GNEISS_ENABLE_PROFILING
      const bool sample = ZoneIsActive;
      const auto before = sample ? sample_thread_execution() : thread_execution_sample{};
#endif
      handle_ = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
#ifdef GNEISS_ENABLE_PROFILING
      // 与 canonical 使用相同口径；失败只标记不可用，不伪造零计数。
      if (sample) {
        const auto after = sample_thread_execution();
        const auto text = thread_execution_label(before, after);
        GNEISS_PROFILE_TEXT(text.data());
      }
#endif
    }
    if (handle_ == INVALID_HANDLE_VALUE) {
      return gneiss::engine::result::io;
    }
    LARGE_INTEGER length{};
    {
      GNEISS_PROFILE_SCOPE("asset.io.file_size");
      if (GetFileSizeEx(handle_, &length) == FALSE || length.QuadPart < 0) {
        return gneiss::engine::result::io;
      }
    }
    length_ = static_cast<std::uint64_t>(length.QuadPart);
    return gneiss::engine::result::success;
  }
  std::uint64_t size() const noexcept override { return length_; }
  [[nodiscard]] gneiss::engine::result
  begin_read(std::uint64_t offset, std::size_t size,
             std::unique_ptr<gneiss::engine::read_operation>& output) const noexcept override {
    GNEISS_PROFILE_SCOPE("asset.io.begin_read");
    output.reset();
    if (offset > length_ || size > length_ - offset || size > std::numeric_limits<DWORD>::max()) {
      return gneiss::engine::result::invalid_argument;
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
            return gneiss::engine::result::io;
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
      if (started == gneiss::engine::result::success) {
        output = std::move(request);
      }
      return started;
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }
  gneiss::engine::result read_at(std::uint64_t offset,
                                 std::span<std::byte> output) const noexcept override {
    if (offset > length_ || output.size() > length_ - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) {
      return gneiss::engine::result::invalid_argument;
    }
    if (output.empty()) {
      return gneiss::engine::result::success;
    }
    try {
      const std::scoped_lock lock(mutex_);
      LARGE_INTEGER position{};
      position.QuadPart = static_cast<LONGLONG>(offset);
      if (SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN) == FALSE) {
        return gneiss::engine::result::io;
      }
      while (!output.empty()) {
        const auto chunk = static_cast<DWORD>(
            std::min<std::size_t>(output.size(), std::numeric_limits<DWORD>::max()));
        DWORD count{};
        if (ReadFile(handle_, output.data(), chunk, &count, nullptr) == FALSE || count != chunk) {
          return gneiss::engine::result::io;
        }
        output = output.subspan(count);
      }
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::io;
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
class native_read_source final : public gneiss::engine::read_source {
public:
  native_read_source(std::ifstream stream, std::uint64_t length)
      : stream_(std::move(stream)), length_(length) {}

  std::uint64_t size() const noexcept override { return length_; }

  gneiss::engine::result read_at(std::uint64_t offset,
                                 std::span<std::byte> output) const noexcept override {
    if (offset > length_ || output.size() > length_ - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        output.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
      return gneiss::engine::result::invalid_argument;
    }
    if (output.empty()) {
      return gneiss::engine::result::success;
    }
    try {
      const std::scoped_lock lock(mutex_);
      stream_.clear();
      stream_.seekg(static_cast<std::streamoff>(offset));
      if (!stream_.read(reinterpret_cast<char*>(output.data()),
                        static_cast<std::streamsize>(output.size()))) {
        return gneiss::engine::result::io;
      }
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::io;
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

namespace gneiss::engine {

#ifdef GNEISS_ASSET_LIBUV
namespace {
class uv_asset_read final : public read_operation {
public:
  std::unique_ptr<gneiss::engine::uv_file_read_operation> operation;
  gneiss::engine::result poll(std::span<const std::byte>& output) noexcept override {
    return operation->poll(output);
  }
  gneiss::engine::result wait_for(std::chrono::milliseconds timeout,
                                  std::span<const std::byte>& output) noexcept override {
    return operation->wait_for(timeout, output);
  }
};
class uv_asset_source final : public read_source {
public:
  std::shared_ptr<gneiss::engine::uv_async_file> source;
  gneiss::engine::result begin_close() const noexcept override {
    source->begin_close();
    return gneiss::engine::result::success;
  }
  gneiss::engine::result poll_close() const noexcept override { return source->poll_close(); }
  [[nodiscard]] std::uint64_t size() const noexcept override { return source->size(); }
  [[nodiscard]] gneiss::engine::result
  read_at(std::uint64_t offset, std::span<std::byte> output) const noexcept override {
    return source->read_at(offset, output);
  }
  gneiss::engine::result
  begin_read(std::uint64_t offset, std::size_t count,
             std::unique_ptr<read_operation>& output) const noexcept override {
    output.reset();
    try {
      auto next = std::make_unique<uv_asset_read>();
      const auto started = source->begin_read(offset, count, next->operation);
      if (started == gneiss::engine::result::success) {
        output = std::move(next);
      }
      return started;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
};
class uv_asset_open final : public open_read_operation {
public:
  std::unique_ptr<gneiss::engine::uv_path_operation> resolving;
  std::shared_ptr<gneiss::engine::uv_file_io> files;
  std::filesystem::path root;
  std::string candidate;
  std::unique_ptr<gneiss::engine::uv_file_open_operation> opening;
  gneiss::engine::result status = gneiss::engine::result::not_ready;
  bool delivered{};
  gneiss::engine::result poll(std::unique_ptr<read_source>& output) noexcept override {
    output.reset();
    if (delivered) {
      return gneiss::engine::result::invalid_state;
    }
    if (status != gneiss::engine::result::not_ready) {
      return status;
    }
    try {
      if (resolving) {
        const auto resolved = resolving->poll(candidate);
        if (resolved == gneiss::engine::result::not_ready) {
          return gneiss::engine::result::not_ready;
        }
        resolving.reset();
        if (resolved != gneiss::engine::result::success) {
          return status = resolved;
        }
        // 两端已规范化，此处仅做词法隔离检查，不再次触发文件系统查询。
        const auto relative = path_from_utf8(candidate).lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
          return status = gneiss::engine::result::invalid_argument;
        }
      }
      if (!opening) {
        const auto submitted = files->begin_open(candidate, opening);
        if (submitted != gneiss::engine::result::success) {
          return status = submitted;
        }
      }
      auto source = std::make_unique<uv_asset_source>();
      const auto received = opening->poll(source->source);
      if (received != gneiss::engine::result::success) {
        return status = received;
      }
      opening.reset();
      delivered = true;
      output = std::move(source);
      return gneiss::engine::result::success;
    } catch (const std::bad_alloc&) {
      return status = gneiss::engine::result::out_of_memory;
    } catch (...) {
      return status = gneiss::engine::result::internal;
    }
  }
};
} // namespace
void native_file_system::set_async_io(
    std::shared_ptr<gneiss::engine::uv_file_io> files,
    std::shared_ptr<gneiss::engine::uv_path_resolver> paths) noexcept {
  async_files_ = std::move(files);
  async_paths_ = std::move(paths);
}
gneiss::engine::result
native_file_system::begin_open_read(std::string_view path,
                                    std::unique_ptr<open_read_operation>& output) const noexcept {
  output.reset();
  if (!async_files_ || !async_paths_) {
    return gneiss::engine::result::unsupported;
  }
  if (root_.empty()) {
    return gneiss::engine::result::invalid_state;
  }
  try {
    if (validate_uri("asset://" + std::string(path)) != gneiss::engine::result::success) {
      return gneiss::engine::result::invalid_argument;
    }
    auto operation = std::make_unique<uv_asset_open>();
    operation->files = async_files_;
    operation->root = root_;
    const auto absolute = (root_ / path_from_utf8(path)).u8string();
    const auto submitted =
        async_paths_->begin(std::string(absolute.begin(), absolute.end()), operation->resolving);
    if (submitted == gneiss::engine::result::success) {
      output = std::move(operation);
    }
    return submitted;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
#endif

gneiss::engine::result
native_file_system::open_read(std::string_view path,
                              std::unique_ptr<read_source>& output) const noexcept {
  GNEISS_PROFILE_SCOPE("asset.fs.open_read");
  GNEISS_PROFILE_TEXT(path);
  output.reset();
  if (root_.empty()) {
    return gneiss::engine::result::invalid_state;
  }
  try {
    if (validate_uri("asset://" + std::string(path)) != gneiss::engine::result::success) {
      return gneiss::engine::result::invalid_argument;
    }
    std::error_code error;
    const auto candidate = [&] {
      GNEISS_PROFILE_SCOPE("asset.fs.canonical");
      GNEISS_PROFILE_TEXT(path);
      const auto input_path = [&] {
        GNEISS_PROFILE_SCOPE("asset.fs.path_build");
        return root_ / path_from_utf8(path);
      }();
      {
        GNEISS_PROFILE_SCOPE("asset.fs.canonical_call");
#if defined(_WIN32) && defined(GNEISS_ENABLE_PROFILING)
        // 仅连接采集时读取线程计数；墙钟包含观测开销，计数不能区分具体等待原因。
        const bool sample = ZoneIsActive;
        const auto before = sample ? sample_thread_execution() : thread_execution_sample{};
        auto result = std::filesystem::canonical(input_path, error);
        if (sample) {
          const auto after = sample_thread_execution();
          if (before.valid && after.valid) {
            std::array<char, 160> text{};
            std::snprintf(text.data(), text.size(), "os_tid=%lu cpu_100ns=%llu cycles=%llu",
                          static_cast<unsigned long>(GetCurrentThreadId()),
                          static_cast<unsigned long long>(after.cpu_ticks - before.cpu_ticks),
                          static_cast<unsigned long long>(after.cycles - before.cycles));
            GNEISS_PROFILE_TEXT(text.data());
          } else {
            GNEISS_PROFILE_TEXT("thread_counters_unavailable");
          }
        }
        return result;
#else
        return std::filesystem::canonical(input_path, error);
#endif
      }
    }();
    if (error) {
      return gneiss::engine::result::not_found;
    }
    const auto regular = [&] {
      GNEISS_PROFILE_SCOPE("asset.fs.file_type");
      return std::filesystem::is_regular_file(candidate, error);
    }();
    if (!regular || error) {
      return gneiss::engine::result::not_found;
    }
    const auto relative = [&] {
      GNEISS_PROFILE_SCOPE("asset.fs.relative");
      return std::filesystem::relative(candidate, root_, error);
    }();
    if (error || relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
      return gneiss::engine::result::invalid_argument;
    }
#ifdef _WIN32
    auto source = std::make_unique<native_read_source>();
    const auto opened = source->open(candidate);
    if (opened != gneiss::engine::result::success) {
      return opened;
    }
    output = std::move(source);
#else
    std::ifstream stream(candidate, std::ios::binary);
    if (!stream) {
      return gneiss::engine::result::io;
    }
    // 从已打开对象获取长度，避免另一次路径查询指向替换后的文件。
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < std::streampos(0)) {
      return gneiss::engine::result::io;
    }
    output =
        std::make_unique<native_read_source>(std::move(stream), static_cast<std::uint64_t>(end));
#endif
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result native_file_system::initialize(std::string_view root) noexcept {
  try {
    const auto path = path_from_utf8(root);
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error) {
      return gneiss::engine::result::not_found;
    }
    root_ = canonical;
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result native_file_system::read(std::string_view path,
                                                std::vector<std::byte>& out_bytes) const noexcept {
  return read_bounded(path, std::numeric_limits<std::size_t>::max(), out_bytes);
}

gneiss::engine::result
native_file_system::read_bounded(std::string_view path, std::size_t limit,
                                 std::vector<std::byte>& out_bytes) const noexcept {
  if (root_.empty()) {
    return gneiss::engine::result::invalid_state;
  }
  try {
    std::string uri = "asset://";
    uri.append(path);
    if (validate_uri(uri) != gneiss::engine::result::success) {
      return gneiss::engine::result::invalid_argument;
    }

    std::error_code error;
    const auto candidate = std::filesystem::canonical(root_ / path_from_utf8(path), error);
    if (error || !std::filesystem::is_regular_file(candidate, error) || error) {
      return gneiss::engine::result::not_found;
    }
    const auto relative = std::filesystem::relative(candidate, root_, error);
    if (error || relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
      return gneiss::engine::result::invalid_argument;
    }
#ifdef GNEISS_ASSET_LIBUV
    const auto utf8_path = candidate.u8string();
    std::unique_ptr<gneiss::engine::uv_file_reader> reader;
    const auto opened = gneiss::engine::uv_file_reader::open(
        std::string(utf8_path.begin(), utf8_path.end()), reader);
    if (opened != gneiss::engine::result::success) {
      return opened;
    }
    const auto size = reader->size();
    if (size > limit || size > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    const auto read = reader->read_at(0U, bytes);
    if (read != gneiss::engine::result::success) {
      return read;
    }
    std::uint64_t final_size{};
    if (reader->current_size(final_size) != gneiss::engine::result::success || final_size != size) {
      return gneiss::engine::result::io;
    }
#else
    std::ifstream stream(candidate, std::ios::binary);
    if (!stream) {
      return gneiss::engine::result::io;
    }
    const auto size = std::filesystem::file_size(candidate, error);
    if (error) {
      return gneiss::engine::result::io;
    }
    if (size > limit ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
      return gneiss::engine::result::invalid_argument;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)) ||
        stream.peek() != std::char_traits<char>::eof()) {
      return gneiss::engine::result::io;
    }
#endif
    out_bytes = std::move(bytes);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

} // namespace gneiss::engine
