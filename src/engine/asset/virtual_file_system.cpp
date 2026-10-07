// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/virtual_file_system.hpp"

#include "engine/asset/asset_uri.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace {

constexpr std::string_view root_mount = "asset://";

[[nodiscard]] bool is_valid_mount_point(std::string_view point) {
  if (point == root_mount) {
    return true;
  }
  if (!point.starts_with(root_mount) || !point.ends_with('/')) {
    return false;
  }
  std::string probe(point);
  probe.push_back('_');
  return gneiss::asset_internal::validate_uri(probe) == GNEISS_SUCCESS;
}

} // namespace

namespace gneiss::asset_internal {

gneiss_result virtual_file_system::open_read(std::string_view uri,
                                             std::unique_ptr<read_source>& output) const noexcept {
  return open_impl(uri, output, false);
}
gneiss_result
virtual_file_system::open_read_for_validation(std::string_view uri,
                                              std::unique_ptr<read_source>& output) const noexcept {
  return open_impl(uri, output, true);
}
gneiss_result virtual_file_system::open_impl(std::string_view uri,
                                             std::unique_ptr<read_source>& output,
                                             bool deferred_validation) const noexcept {
  output.reset();
  if (validate_uri(uri) != GNEISS_SUCCESS) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const mount_entry* selected = nullptr;
  for (const auto& entry : mounts_) {
    if (uri.starts_with(entry.point) &&
        (selected == nullptr || entry.point.size() > selected->point.size())) {
      selected = &entry;
    }
  }
  if (selected == nullptr) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  return deferred_validation
             ? selected->backend->open_read_for_validation(uri.substr(selected->point.size()),
                                                           output)
             : selected->backend->open_read(uri.substr(selected->point.size()), output);
}

gneiss_result virtual_file_system::mount(std::string_view mount_point,
                                         std::shared_ptr<file_system> backend) noexcept {
  if (backend == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    if (!is_valid_mount_point(mount_point)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (std::ranges::any_of(mounts_, [mount_point](const mount_entry& entry) {
          return entry.point == mount_point;
        })) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    mounts_.push_back({.point = std::string(mount_point), .backend = std::move(backend)});
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result virtual_file_system::read(std::string_view uri,
                                        std::vector<std::byte>& out_bytes) const noexcept {
  return read_bounded(uri, std::numeric_limits<std::size_t>::max(), out_bytes);
}

gneiss_result virtual_file_system::read_bounded(std::string_view uri, std::size_t limit,
                                                std::vector<std::byte>& out_bytes) const noexcept {
  if (validate_uri(uri) != GNEISS_SUCCESS) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const mount_entry* selected = nullptr;
  for (const auto& entry : mounts_) {
    if (uri.starts_with(entry.point) &&
        (selected == nullptr || entry.point.size() > selected->point.size())) {
      selected = &entry;
    }
  }
  if (selected == nullptr) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  const auto path = uri.substr(selected->point.size());
  return limit == std::numeric_limits<std::size_t>::max()
             ? selected->backend->read(path, out_bytes)
             : selected->backend->read_bounded(path, limit, out_bytes);
}

} // namespace gneiss::asset_internal
