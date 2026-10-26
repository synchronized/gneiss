// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/platform/files/file_read_stream.hpp"
#include <algorithm>
#include <stdexcept>
namespace gneiss::engine {
struct file_read_stream::state {
  std::shared_ptr<file_service> files;
  std::string uri;
  std::uint64_t size{}, offset{};
  std::size_t block_bytes{};
  file_request_id request;
  bool pending{}, ended{};
};
file_read_stream::file_read_stream(std::shared_ptr<file_service> files, std::string uri,
                                   std::uint64_t size, std::size_t block_bytes)
    : state_(std::make_shared<state>()) {
  if (!files || block_bytes == 0)
    throw std::invalid_argument("文件流配置无效");
  state_->files = std::move(files);
  state_->uri = std::move(uri);
  state_->size = size;
  state_->block_bytes = block_bytes;
}
file_read_stream::~file_read_stream() {
  if (state_->pending)
    (void)state_->files->cancel(state_->request);
}
result file_read_stream::cancel() noexcept {
  return state_->pending ? state_->files->cancel(state_->request) : result::invalid_state;
}
result file_read_stream::read_next(file_callback<file_block> callback) noexcept try {
  auto& s = *state_;
  if (!callback)
    return result::invalid_argument;
  if (s.ended)
    return result::invalid_state;
  if (s.pending)
    return result::not_ready;
  const auto length =
      static_cast<std::size_t>(std::min<std::uint64_t>(s.block_bytes, s.size - s.offset));
  const auto offset = s.offset;
  const auto status = s.files->read_range(
      s.uri, {offset, length},
      [weak = std::weak_ptr<state>(state_), offset, length,
       callback = std::move(callback)](file_completion<file_bytes> value) mutable {
        const auto current = weak.lock();
        if (!current)
          return;
        current->pending = false;
        current->request = {};
        if (value.status.ok() && value.data.size() != length)
          value.status = result::io;
        if (value.status.failed()) {
          callback({value.status, {}});
          return;
        }
        current->offset = offset + length;
        current->ended = current->offset == current->size;
        callback({result::success, {std::move(value.data), offset, current->ended}});
      },
      s.request);
  if (status.ok())
    s.pending = true;
  return status;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
}
