// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/vfs/file_system.hpp"

#include <functional>
#include <limits>
#include <string>
#include <utility>

namespace gneiss::test_support {
/** 仅用于内存夹具：在非阻塞 poll 中获取内存快照，不适用于磁盘/网络后端。 */
class memory_read_file_system : public engine::file_system,
                                public std::enable_shared_from_this<memory_read_file_system> {
  using bytes = std::vector<std::byte>;
  class part final : public engine::read_operation {
  public:
    std::shared_ptr<const bytes> data;
    std::size_t offset{}, length{};
    engine::result poll(std::span<const std::byte>& output) noexcept override {
      output = std::span(*data).subspan(offset, length);
      return engine::result::success;
    }
  };
  class source final : public engine::read_source {
  public:
    std::shared_ptr<const bytes> data;
    std::uint64_t size() const noexcept override { return data->size(); }
    engine::result read_at(std::uint64_t, std::span<std::byte>) const noexcept override {
      return engine::result::unsupported;
    }
    engine::result
    begin_read(std::uint64_t offset, std::size_t length,
               std::unique_ptr<engine::read_operation>& output) const noexcept override try {
      if (offset > data->size() || length > data->size() - offset)
        return engine::result::invalid_argument;
      auto pending = std::make_unique<part>();
      pending->data = data;
      pending->offset = static_cast<std::size_t>(offset);
      pending->length = length;
      output = std::move(pending);
      return engine::result::success;
    } catch (...) {
      return engine::result::out_of_memory;
    }
    engine::result begin_close() const noexcept override { return engine::result::success; }
    engine::result poll_close() const noexcept override { return engine::result::success; }
  };
  class opening final : public engine::open_read_operation {
  public:
    std::shared_ptr<const memory_read_file_system> owner;
    std::string path;
    engine::result poll(std::unique_ptr<engine::read_source>& output) noexcept override try {
      auto data = std::make_shared<bytes>();
      const auto status = owner->read_bounded(path, std::numeric_limits<std::size_t>::max(), *data);
      if (status.failed())
        return status;
      auto opened = std::make_unique<source>();
      opened->data = std::move(data);
      output = std::move(opened);
      return engine::result::success;
    } catch (...) {
      return engine::result::out_of_memory;
    }
  };

public:
  engine::result
  begin_open_read(std::string_view path,
                  std::unique_ptr<engine::open_read_operation>& output) const noexcept override
      try {
    auto pending = std::make_unique<opening>();
    pending->owner = shared_from_this();
    pending->path = path;
    output = std::move(pending);
    return engine::result::success;
  } catch (...) {
    return engine::result::out_of_memory;
  }
};
} // namespace gneiss::test_support
