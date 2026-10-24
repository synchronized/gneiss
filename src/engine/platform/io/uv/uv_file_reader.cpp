// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_file_reader.hpp"
#include "engine/core/result.hpp"
#include "uv_result.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <uv.h>

#include <algorithm>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <sys/stat.h>
#include <utility>

namespace gneiss::engine::platform::io {
namespace {
struct request final {
  uv_fs_t value{};
  ~request() { uv_fs_req_cleanup(&value); }
};
void close_file(int descriptor) noexcept {
  request closed;
  (void)uv_fs_close(nullptr, &closed.value, descriptor, nullptr);
}
} // namespace

struct uv_file_reader::implementation final {
  std::mutex mutex;
};

uv_file_reader::uv_file_reader(int descriptor) noexcept : descriptor_(descriptor) {}

uv_file_reader::~uv_file_reader() { close_file(descriptor_); }

gneiss::engine::core::result
uv_file_reader::open(std::string_view path, std::unique_ptr<uv_file_reader>& output) noexcept {
  output.reset();
  if (path.empty() || path.find('\0') != std::string_view::npos) {
    return gneiss::engine::core::result::invalid_argument;
  }
  try {
    const std::string terminated(path);
    request opened;
    const int descriptor =
        uv_fs_open(nullptr, &opened.value, terminated.c_str(), O_RDONLY, 0, nullptr);
    if (descriptor < 0) {
      return from_uv_status(descriptor);
    }
    // 先将描述符交给 RAII 对象，后续查询或分配失败均关闭它。
    std::unique_ptr<uv_file_reader> reader(new (std::nothrow) uv_file_reader(descriptor));
    if (!reader) {
      close_file(descriptor);
      return gneiss::engine::core::result::out_of_memory;
    }
    reader->implementation_ = std::make_unique<implementation>();
    request info;
    const int status = uv_fs_fstat(nullptr, &info.value, descriptor, nullptr);
    if (status < 0) {
      return from_uv_status(status);
    }
    if ((info.value.statbuf.st_mode & S_IFMT) != S_IFREG) {
      return gneiss::engine::core::result::invalid_argument;
    }
    reader->size_ = info.value.statbuf.st_size;
    output = std::move(reader);
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result uv_file_reader::current_size(std::uint64_t& output) const noexcept {
  output = 0U;
  request info;
  const int status = uv_fs_fstat(nullptr, &info.value, descriptor_, nullptr);
  if (status < 0) {
    return from_uv_status(status);
  }
  output = info.value.statbuf.st_size;
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result uv_file_reader::read_at(std::uint64_t offset,
                                                     std::span<std::byte> output) const noexcept {
  if (offset > size_ || output.size() > size_ - offset ||
      offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
      output.size() >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - offset) {
    return gneiss::engine::core::result::invalid_argument;
  }
  try {
    const std::scoped_lock lock(implementation_->mutex);
    while (!output.empty()) {
      const auto count = static_cast<unsigned int>(
          std::min(output.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
      auto buffer = uv_buf_init(reinterpret_cast<char*>(output.data()), count);
      request read;
      const auto status = uv_fs_read(nullptr, &read.value, descriptor_, &buffer, 1U,
                                     static_cast<std::int64_t>(offset), nullptr);
      if (status < 0) {
        return from_uv_status(status);
      }
      if (read.value.result <= 0 || std::cmp_greater(read.value.result, count)) {
        return gneiss::engine::core::result::io;
      }
      const auto consumed = static_cast<std::size_t>(read.value.result);
      offset += consumed;
      output = output.subspan(consumed);
    }
    return gneiss::engine::core::result::success;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

} // namespace gneiss::engine::platform::io
