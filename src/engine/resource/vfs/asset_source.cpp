// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#include "asset_source.hpp"
#include "engine/core/result.hpp"
#include <algorithm>

namespace gneiss::engine {
asset_source::asset_source(virtual_file_system files, std::string uri, std::size_t limit)
    : files_(std::move(files)), uri_(std::move(uri)), limit_(limit) {}
gneiss::engine::result asset_source::advance() {
  if (limit_ == 0) {
    return gneiss::engine::result::invalid_argument;
  }
  if (!started_) {
    started_ = true;
    auto status = files_.begin_open_read(uri_, opening_);
    if (status == gneiss::engine::result::unsupported) {
      status = files_.open_read(uri_, source_);
      if (status == gneiss::engine::result::unsupported) {
        return files_.read_bounded(uri_, limit_, bytes_);
      }
    }
    if (status != gneiss::engine::result::success) {
      return status;
    }
  }
  if (opening_) {
    const auto status = opening_->poll(source_);
    if (status != gneiss::engine::result::success) {
      return status;
    }
    opening_.reset();
  }
  if (!source_) {
    return gneiss::engine::result::internal;
  }
  if (source_->size() > limit_) {
    return gneiss::engine::result::invalid_argument;
  }
  return read_contents();
}
gneiss::engine::result asset_source::read_contents() {
  if (closing_) {
    const auto status = source_->poll_close();
    return status == gneiss::engine::result::unsupported ? gneiss::engine::result::success : status;
  }
  if (reading_) {
    std::span<const std::byte> part;
    const auto status = reading_->poll(part);
    if (status != gneiss::engine::result::success) {
      return status;
    }
    const auto expected = std::min<std::size_t>(65536U, source_->size() - bytes_.size());
    if (part.size() != expected) {
      return gneiss::engine::result::io;
    }
    bytes_.insert(bytes_.end(), part.begin(), part.end());
    reading_.reset();
  }
  if (bytes_.size() == source_->size()) {
    closing_ = true;
    auto status = source_->begin_close();
    if (status == gneiss::engine::result::unsupported) {
      return gneiss::engine::result::success;
    }
    return status == gneiss::engine::result::success ? gneiss::engine::result::not_ready : status;
  }
  const auto length = std::min<std::size_t>(65536U, source_->size() - bytes_.size());
  auto status = source_->begin_read(bytes_.size(), length, reading_);
  if (status == gneiss::engine::result::unsupported) {
    const auto offset = bytes_.size();
    bytes_.resize(offset + length);
    status = source_->read_at(offset, std::span(bytes_).subspan(offset));
  }
  return status == gneiss::engine::result::success ? gneiss::engine::result::not_ready : status;
}
}
