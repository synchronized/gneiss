// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/platform/files/file_read_stream.hpp"
#include "engine/platform/files/file_backend.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace gneiss::engine {
struct file_read_stream::state : std::enable_shared_from_this<state> {
  std::shared_ptr<file_service> files;
  std::string uri;
  std::optional<std::uint64_t> size;
  std::uint64_t offset{};
  std::shared_ptr<file_backend_stream> source;
  std::size_t block_bytes{};
  file_request_id request;
  bool pending{}, ended{};
  ~state() {
    if (source)
      source->close();
  }
  result next(file_callback<file_block> callback) {
    const auto status = files->read_stream_source(
        uri, source, block_bytes,
        [weak = weak_from_this(),
         callback = std::move(callback)](file_completion<file_block> value) mutable {
          const auto current = weak.lock();
          if (!current)
            return;
          current->pending = false;
          current->request = {};
          if (value.status.ok() &&
              (value.data.bytes.size() > current->block_bytes ||
               value.data.offset != current->offset ||
               (!value.data.eof && value.data.bytes.empty()) ||
               value.data.bytes.size() >
                   std::numeric_limits<std::uint64_t>::max() - current->offset))
            value.status = result::io;
          if (value.status.failed()) {
            value.data = {};
            current->ended = true;
          } else {
            current->offset += value.data.bytes.size();
            current->ended = value.data.eof;
          }
          if (current->ended)
            current->source->close();
          callback(std::move(value));
        },
        request);
    pending = status.ok();
    return status;
  }
};
file_read_stream::file_read_stream(std::shared_ptr<file_service> files, std::string uri,
                                   std::optional<std::uint64_t> size, std::size_t block_bytes)
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
  auto& s = *state_;
  if (s.size)
    return s.pending ? s.files->cancel(s.request) : result::invalid_state;
  if (s.ended)
    return result::invalid_state;
  const auto status = s.pending ? s.files->cancel(s.request) : result::success;
  if (status.ok()) {
    s.ended = true;
    if (s.source)
      s.source->close();
  }
  return status;
}
result file_read_stream::read_sequential(file_callback<file_block> callback) noexcept try {
  auto& s = *state_;
  if (s.source)
    return s.next(std::move(callback));
  const auto status = s.files->open_stream_source(
      s.uri, s.block_bytes,
      [weak = std::weak_ptr<state>(state_), callback = std::move(callback)](auto opened) mutable {
        const auto current = weak.lock();
        if (!current) {
          if (opened.data)
            opened.data->close();
          return;
        }
        current->pending = false;
        current->request = {};
        if (opened.status.failed()) {
          current->ended = true;
          callback({opened.status, {}});
          return;
        }
        current->source = std::move(opened.data);
        result submitted;
        try {
          submitted = current->next(callback);
        } catch (...) {
          submitted = result::out_of_memory;
        }
        if (submitted.failed())
          callback({submitted, {}});
      },
      s.request);
  s.pending = status.ok();
  return status;
} catch (...) {
  return result::out_of_memory;
}
result file_read_stream::read_next(file_callback<file_block> callback) noexcept try {
  auto& s = *state_;
  if (!callback)
    return result::invalid_argument;
  if (s.ended)
    return result::invalid_state;
  if (s.pending)
    return result::not_ready;
  if (!s.size)
    return read_sequential(std::move(callback));
  const auto length =
      static_cast<std::size_t>(std::min<std::uint64_t>(s.block_bytes, *s.size - s.offset));
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
        current->ended = current->offset == *current->size;
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
