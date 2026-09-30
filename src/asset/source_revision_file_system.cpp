// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/source_revision_file_system.h"

#include <algorithm>
#include <array>
#include <limits>

namespace gneiss::asset_internal {
namespace {
gneiss_result hash_source(const read_source& source, core::sha256_digest& digest,
                          const std::function<bool()>& cancelled) {
  std::array<std::byte, std::size_t{64U} * 1024U> buffer{};
  core::sha256_builder builder;
  for (std::uint64_t offset = 0U; offset < source.size();) {
    if (cancelled && cancelled()) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    const auto count =
        static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), source.size() - offset));
    const auto chunk = std::span{buffer}.first(count);
    const auto result = source.read_at(offset, chunk);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    builder.update(chunk);
    offset += count;
  }
  if (cancelled && cancelled()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  digest = builder.digest();
  return GNEISS_SUCCESS;
}
} // namespace

gneiss_result source_revision_file_system::remember(std::string_view path,
                                                    const identity& current) const {
  const std::scoped_lock lock(mutex_);
  const auto found = identities_.find(std::string(path));
  if (found != identities_.end()) {
    return found->second.bytes == current.bytes && found->second.digest == current.digest
               ? GNEISS_SUCCESS
               : GNEISS_ERROR_INVALID_STATE;
  }
  if (identities_.size() >= 65536U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  identities_.emplace(path, current);
  return GNEISS_SUCCESS;
}

gneiss_result
source_revision_file_system::open_read(std::string_view path,
                                       std::unique_ptr<read_source>& output) const noexcept {
  output.reset();
  try {
    std::unique_ptr<read_source> source;
    auto result = source_.open_read("asset://" + std::string(path), source);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    identity current{.bytes = source->size(), .digest = {}};
    result = hash_source(*source, current.digest, cancelled_);
    if (result == GNEISS_SUCCESS) {
      result = remember(path, current);
    }
    if (result == GNEISS_SUCCESS) {
      output = std::move(source);
    }
    return result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result source_revision_file_system::read(std::string_view path,
                                                std::vector<std::byte>& bytes) const noexcept {
  return read_bounded(path, std::size_t{256U} * 1024U * 1024U, bytes);
}
gneiss_result
source_revision_file_system::read_bounded(std::string_view path, std::size_t limit,
                                          std::vector<std::byte>& bytes) const noexcept {
  try {
    const auto result = source_.read_bounded("asset://" + std::string(path), limit, bytes);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    const identity current{.bytes = bytes.size(), .digest = core::sha256(bytes)};
    const auto recorded = remember(path, current);
    if (recorded != GNEISS_SUCCESS) {
      bytes.clear();
    }
    return recorded;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result
source_revision_file_system::verify(const std::function<bool()>& cancelled) const noexcept {
  try {
    std::map<std::string, identity> sources;
    {
      const std::scoped_lock lock(mutex_);
      sources = identities_;
    }
    for (const auto& [path, expected] : sources) {
      if (cancelled && cancelled()) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      std::unique_ptr<read_source> source;
      const auto opened = source_.open_read("asset://" + path, source);
      if (opened == GNEISS_SUCCESS) {
        core::sha256_digest digest{};
        if (source->size() != expected.bytes ||
            hash_source(*source, digest, cancelled) != GNEISS_SUCCESS ||
            digest != expected.digest) {
          return GNEISS_ERROR_INVALID_STATE;
        }
      } else if (opened == GNEISS_ERROR_UNSUPPORTED) {
        // 兼容既有只支持整文件的后端；新来源错误不得降级后重试掩盖失败。
        std::vector<std::byte> bytes;
        if (expected.bytes > std::numeric_limits<std::size_t>::max() ||
            source_.read_bounded("asset://" + path, static_cast<std::size_t>(expected.bytes),
                                 bytes) != GNEISS_SUCCESS ||
            bytes.size() != expected.bytes || core::sha256(bytes) != expected.digest) {
          return GNEISS_ERROR_INVALID_STATE;
        }
      } else {
        return GNEISS_ERROR_INVALID_STATE;
      }
    }
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
std::size_t source_revision_file_system::source_count() const {
  const std::scoped_lock lock(mutex_);
  return identities_.size();
}

} // namespace gneiss::asset_internal
