// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#include "asset_source.hpp"
#include <algorithm>

namespace gneiss::engine::resource {
asset_source::asset_source(virtual_file_system files, std::string uri, std::size_t limit)
    : files_(std::move(files)), uri_(std::move(uri)), limit_(limit) {}
result asset_source::advance() {
  if (limit_ == 0) {
    return result::invalid_argument;
  }
  if (!started_) {
    started_ = true;
    auto status = from_native(files_.begin_open_read_for_validation(uri_, opening_));
    if (status == result::unsupported) {
      status = from_native(files_.open_read_for_validation(uri_, source_));
      if (status == result::unsupported) {
        return from_native(files_.read_bounded(uri_, limit_, bytes_));
      }
    }
    if (status != result::success) {
      return status;
    }
  }
  if (opening_) {
    const auto status = from_native(opening_->poll(source_));
    if (status != result::success) {
      return status;
    }
    opening_.reset();
  }
  if (!source_) {
    return result::internal;
  }
  if (source_->size() > limit_) {
    return result::invalid_argument;
  }
  return read_contents();
}
result asset_source::read_contents() {
  if (closing_) {
    const auto status = from_native(source_->poll_close());
    return status == result::unsupported ? result::success : status;
  }
  if (reading_) {
    std::span<const std::byte> part;
    const auto status = from_native(reading_->poll(part));
    if (status != result::success) {
      return status;
    }
    const auto expected = std::min<std::size_t>(65536U, source_->size() - bytes_.size());
    if (part.size() != expected) {
      return result::io;
    }
    bytes_.insert(bytes_.end(), part.begin(), part.end());
    reading_.reset();
  }
  if (bytes_.size() == source_->size()) {
    auto status = from_native(source_->complete_validation(engine::core::sha256(bytes_)));
    if (status != result::success) {
      return status;
    }
    closing_ = true;
    status = from_native(source_->begin_close());
    if (status == result::unsupported) {
      return result::success;
    }
    return status == result::success ? result::not_ready : status;
  }
  const auto length = std::min<std::size_t>(65536U, source_->size() - bytes_.size());
  auto status = from_native(source_->begin_read(bytes_.size(), length, reading_));
  if (status == result::unsupported) {
    const auto offset = bytes_.size();
    bytes_.resize(offset + length);
    status = from_native(source_->read_at(offset, std::span(bytes_).subspan(offset)));
  }
  return status == result::success ? result::not_ready : status;
}
}
