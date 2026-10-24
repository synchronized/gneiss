// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/vfs/virtual_file_system.hpp"
#include "engine/core/result.hpp"

#include "engine/resource/vfs/asset_uri.hpp"

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
  return gneiss::engine::resource::validate_uri(probe) == gneiss::engine::core::result::success;
}

} // namespace

namespace gneiss::engine::resource {

gneiss::engine::core::result virtual_file_system::resolve_source(
    std::string_view uri, std::shared_ptr<const asset_source_domain>& output) const noexcept {
  output.reset();
  if (validate_uri(uri) != gneiss::engine::core::result::success)
    return gneiss::engine::core::result::invalid_argument;
  const mount_entry* selected = nullptr;
  for (const auto& entry : mounts_) {
    if (uri.starts_with(entry.point) &&
        (selected == nullptr || entry.point.size() > selected->point.size())) {
      selected = &entry;
    }
  }
  if (selected == nullptr) {
    return gneiss::engine::core::result::not_found;
  }
  output = selected->source;
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result virtual_file_system::identify(std::string_view uri, std::uint32_t type,
                                                           asset_identity& output) const noexcept {
  output = {};
  if (type == 0U || validate_uri(uri) != gneiss::engine::core::result::success) {
    return gneiss::engine::core::result::invalid_argument;
  }
  std::shared_ptr<const asset_source_domain> source;
  const auto resolved = resolve_source(uri, source);
  if (resolved != gneiss::engine::core::result::success)
    return resolved;
  try {
    asset_identity identity{.source = std::move(source), .uri = std::string(uri), .type = type};
    output = std::move(identity);
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
virtual_file_system::open_read(std::string_view uri,
                               std::unique_ptr<read_source>& output) const noexcept {
  output.reset();
  if (validate_uri(uri) != gneiss::engine::core::result::success) {
    return gneiss::engine::core::result::invalid_argument;
  }
  const mount_entry* selected = nullptr;
  for (const auto& entry : mounts_) {
    if (uri.starts_with(entry.point) &&
        (selected == nullptr || entry.point.size() > selected->point.size())) {
      selected = &entry;
    }
  }
  if (selected == nullptr) {
    return gneiss::engine::core::result::not_found;
  }
  return selected->backend->open_read(uri.substr(selected->point.size()), output);
}

gneiss::engine::core::result
virtual_file_system::begin_open_read(std::string_view uri,
                                     std::unique_ptr<open_read_operation>& output) const noexcept {
  output.reset();
  if (validate_uri(uri) != gneiss::engine::core::result::success) {
    return gneiss::engine::core::result::invalid_argument;
  }
  const mount_entry* selected = nullptr;
  for (const auto& entry : mounts_) {
    if (uri.starts_with(entry.point) &&
        (selected == nullptr || entry.point.size() > selected->point.size())) {
      selected = &entry;
    }
  }
  if (selected == nullptr) {
    return gneiss::engine::core::result::not_found;
  }
  return selected->backend->begin_open_read(uri.substr(selected->point.size()), output);
}

gneiss::engine::core::result
virtual_file_system::mount(std::string_view mount_point,
                           std::shared_ptr<file_system> backend) noexcept {
  if (backend == nullptr) {
    return gneiss::engine::core::result::invalid_argument;
  }
  try {
    if (!is_valid_mount_point(mount_point)) {
      return gneiss::engine::core::result::invalid_argument;
    }
    if (std::ranges::any_of(mounts_, [mount_point](const mount_entry& entry) {
          return entry.point == mount_point;
        })) {
      return gneiss::engine::core::result::invalid_state;
    }
    mounts_.push_back({
        .point = std::string(mount_point),
        .backend = std::move(backend),
        .source = std::make_shared<const asset_source_domain>(),
    });
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
virtual_file_system::read(std::string_view uri, std::vector<std::byte>& out_bytes) const noexcept {
  return read_bounded(uri, std::numeric_limits<std::size_t>::max(), out_bytes);
}

gneiss::engine::core::result
virtual_file_system::read_bounded(std::string_view uri, std::size_t limit,
                                  std::vector<std::byte>& out_bytes) const noexcept {
  if (validate_uri(uri) != gneiss::engine::core::result::success) {
    return gneiss::engine::core::result::invalid_argument;
  }
  const mount_entry* selected = nullptr;
  for (const auto& entry : mounts_) {
    if (uri.starts_with(entry.point) &&
        (selected == nullptr || entry.point.size() > selected->point.size())) {
      selected = &entry;
    }
  }
  if (selected == nullptr) {
    return gneiss::engine::core::result::not_found;
  }
  const auto path = uri.substr(selected->point.size());
  return limit == std::numeric_limits<std::size_t>::max()
             ? selected->backend->read(path, out_bytes)
             : selected->backend->read_bounded(path, limit, out_bytes);
}

} // namespace gneiss::engine::resource
