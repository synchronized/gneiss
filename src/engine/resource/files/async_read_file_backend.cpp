// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/files/async_read_file_backend.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include <stdexcept>

#include "engine/platform/files/file_path.hpp"
#include "engine/resource/vfs/file_system.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace gneiss::engine {
struct async_read_file_backend::implementation {
  struct operation {
    std::unique_ptr<open_read_operation> opening;
    std::unique_ptr<read_source> source;
    std::unique_ptr<read_operation> reading;
    file_callback<file_bytes> callback;
    file_bytes bytes;
    std::optional<file_range> range;
    std::size_t limit{}, count{}, chunk{};
    std::uint64_t offset{};
    result status{result::success};
    bool closing{};

    bool close(result value) {
      status = value;
      reading.reset();
      if (!source)
        return true;
      const auto started = source->begin_close();
      if (started != result::success) {
        if (status == result::success)
          status = started;
        source.reset();
        return true;
      }
      closing = true;
      return false;
    }
    bool advance() {
      if (closing) {
        const auto closed = source->poll_close();
        if (closed == result::not_ready)
          return false;
        if (status == result::success)
          status = closed;
        source.reset();
        return true;
      }
      if (opening) {
        const auto opened = opening->poll(source);
        if (opened == result::not_ready)
          return false;
        opening.reset();
        if (opened != result::success)
          return close(opened);
        if (!source)
          return close(result::internal);
        if (range) {
          if (range->offset > source->size() || range->size > source->size() - range->offset)
            return close(result::invalid_argument);
          offset = range->offset;
          count = range->size;
        } else {
          if (source->size() > limit)
            return close(result::invalid_argument);
          count = static_cast<std::size_t>(source->size());
        }
      }
      if (reading) {
        std::span<const std::byte> part;
        const auto received = reading->poll(part);
        if (received == result::not_ready)
          return false;
        if (received != result::success)
          return close(received);
        if (part.size() != chunk)
          return close(result::io);
        bytes.insert(bytes.end(), part.begin(), part.end());
        reading.reset();
      }
      if (bytes.size() == count)
        return close(result::success);
      chunk = std::min<std::size_t>(65536, count - bytes.size());
      const auto started = source->begin_read(offset + bytes.size(), chunk, reading);
      // 后端临时满载只重试提交；不在服务线程等待或同步回退。
      if (started == result::not_ready)
        return false;
      if (started != result::success)
        return close(started);
      if (!reading)
        return close(result::internal);
      return false;
    }
  };
  std::shared_ptr<file_system> backend;
  std::vector<std::unique_ptr<operation>> pending;

  result begin(std::string_view path, std::size_t limit, std::optional<file_range> range,
               file_callback<file_bytes> callback) {
    if (!backend || !callback || !detail::valid_file_path(path))
      return result::invalid_argument;
    auto request = std::make_unique<operation>();
    request->limit = limit;
    request->range = range;
    request->callback = std::move(callback);
    // 先保留容器空间，避免已提交后插入失败而丢失完成回调。
    pending.reserve(pending.size() + 1);
    const auto started = backend->begin_open_read(path, request->opening);
    if (started != result::success)
      return started;
    if (!request->opening)
      return result::internal;
    pending.push_back(std::move(request));
    return result::success;
  }
};
async_read_file_backend::async_read_file_backend(std::shared_ptr<file_system> backend)
    : impl_(std::make_unique<implementation>()) {
  impl_->backend = std::move(backend);
}
async_read_file_backend::~async_read_file_backend() = default;
result async_read_file_backend::read(std::string_view path, read_options options,
                                     file_callback<file_bytes> callback) {
  if (options.max_bytes == 0)
    return result::invalid_argument;
  return impl_->begin(path, options.max_bytes, {}, std::move(callback));
}
result async_read_file_backend::read_range(std::string_view path, file_range range,
                                           file_callback<file_bytes> callback) {
  if (range.offset > std::numeric_limits<std::uint64_t>::max() - range.size)
    return result::invalid_argument;
  return impl_->begin(path, range.size, range, std::move(callback));
}
void async_read_file_backend::poll() noexcept {
  for (auto it = impl_->pending.begin(); it != impl_->pending.end();) {
    auto& request = **it;
    bool ready{};
    try {
      ready = request.advance();
    } catch (const std::bad_alloc&) {
      ready = request.close(result::out_of_memory);
    } catch (...) {
      ready = request.close(result::internal);
    }
    if (!ready) {
      ++it;
      continue;
    }
    auto finished = std::move(*it);
    it = impl_->pending.erase(it);
    if (finished->status != result::success)
      finished->bytes.clear();
    finished->callback({finished->status, std::move(finished->bytes)});
  }
}
} // namespace gneiss::engine

namespace gneiss::engine {
std::shared_ptr<file_service> make_file_service(const virtual_file_system& files,
                                                file_service_options options) {
  auto service = std::make_shared<file_service>(options);
  for (const auto& entry : files.mounts_) {
    const auto status = service->mount({entry.point,
                                        {std::make_shared<async_read_file_backend>(entry.backend)},
                                        {},
                                        entry.source});
    if (status == result::out_of_memory)
      throw std::bad_alloc{};
    if (status.failed())
      throw std::invalid_argument("无法导入文件挂载");
  }
  return service;
}
}
