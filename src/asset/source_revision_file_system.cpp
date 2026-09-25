// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/source_revision_file_system.h"

namespace gneiss::asset_internal {

gneiss_result source_revision_file_system::read(std::string_view path,
                                                std::vector<std::byte>& bytes) const noexcept {
  return read_bounded(path, 256U * 1024U * 1024U, bytes);
}
gneiss_result
source_revision_file_system::read_bounded(std::string_view path, std::size_t limit,
                                          std::vector<std::byte>& bytes) const noexcept {
  try {
    const auto result = source_.read_bounded("asset://" + std::string(path), limit, bytes);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    const identity current{bytes.size(), core::sha256(bytes)};
    const std::scoped_lock lock(mutex_);
    const auto found = identities_.find(std::string(path));
    if (found != identities_.end()) {
      if (found->second.bytes != current.bytes || found->second.digest != current.digest) {
        bytes.clear();
        return GNEISS_ERROR_INVALID_STATE;
      }
    } else {
      if (identities_.size() >= 65536U) {
        bytes.clear();
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      identities_.emplace(path, current);
    }
    return GNEISS_SUCCESS;
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
      std::vector<std::byte> bytes;
      if (source_.read_bounded("asset://" + path, expected.bytes, bytes) != GNEISS_SUCCESS ||
          bytes.size() != expected.bytes || core::sha256(bytes) != expected.digest) {
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
