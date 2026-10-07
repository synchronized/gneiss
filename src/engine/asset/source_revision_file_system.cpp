// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/source_revision_file_system.hpp"
#include "engine/asset/read_slice.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>

namespace gneiss::asset_internal {
namespace {
using clock_type = std::chrono::steady_clock;
void record_duration(clock_type::time_point start, double& maximum) {
  maximum = std::max(maximum,
                     std::chrono::duration<double, std::milli>(clock_type::now() - start).count());
}
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

gneiss_result source_revision_file_system::remember(const std::shared_ptr<ledger>& versions,
                                                    std::string_view path,
                                                    const identity& current) {
  const std::scoped_lock lock(versions->mutex);
  const auto found = versions->identities.find(std::string(path));
  if (found != versions->identities.end()) {
    return found->second.bytes == current.bytes && found->second.digest == current.digest
               ? GNEISS_SUCCESS
               : GNEISS_ERROR_INVALID_STATE;
  }
  if (versions->identities.size() >= 65536U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  versions->identities.emplace(path, current);
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
      result = source->complete_validation(current.digest);
      if (result == GNEISS_SUCCESS) {
        result = remember(versions_, path, current);
      }
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

namespace {
class tracked_read_source final : public read_source {
public:
  using commit_function = std::function<gneiss_result(const core::sha256_digest&)>;
  tracked_read_source(std::unique_ptr<read_source> source, commit_function commit)
      : source_(std::move(source)), commit_(std::move(commit)) {}
  [[nodiscard]] gneiss_result
  begin_read(std::uint64_t offset, std::size_t size,
             std::unique_ptr<read_operation>& output) const noexcept override {
    return source_->begin_read(offset, size, output);
  }
  [[nodiscard]] std::uint64_t size() const noexcept override { return source_->size(); }
  [[nodiscard]] gneiss_result read_at(std::uint64_t offset,
                                      std::span<std::byte> output) const noexcept override {
    return source_->read_at(offset, output);
  }
  [[nodiscard]] gneiss_result
  complete_validation(const core::sha256_digest& digest) const noexcept override {
    try {
      const auto result = source_->complete_validation(digest);
      return result == GNEISS_SUCCESS ? commit_(digest) : result;
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

private:
  std::unique_ptr<read_source> source_;
  commit_function commit_;
};
} // namespace

gneiss_result source_revision_file_system::open_read_for_validation(
    std::string_view path, std::unique_ptr<read_source>& output) const noexcept {
  output.reset();
  try {
    std::unique_ptr<read_source> source;
    const auto result = source_.open_read_for_validation("asset://" + std::string(path), source);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    const auto bytes = source->size();
    output = std::make_unique<tracked_read_source>(
        std::move(source),
        [versions = versions_, path = std::string(path), bytes](const auto& digest) {
          return remember(versions, path, {.bytes = bytes, .digest = digest});
        });
    return GNEISS_SUCCESS;
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
    const auto recorded = remember(versions_, path, current);
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
struct source_revision_file_system::verification::state {
  virtual_file_system files;
  std::vector<std::pair<std::string, identity>> identities;
  std::size_t index{};
  std::unique_ptr<read_source> source;
  core::sha256_builder digest;
  std::uint64_t offset{};
  gneiss_result result{GNEISS_SUCCESS};
  statistics timings;
  read_slice slice;
  bool deferred_reads{true};
  std::chrono::milliseconds io_wait{};
  gneiss_result read_next(std::size_t& byte_budget, std::uint64_t expected_bytes) {
    const auto count =
        static_cast<std::size_t>(std::min<std::uint64_t>(byte_budget, expected_bytes - offset));
    if (count != 0U) {
      std::span<const std::byte> chunk;
      const auto read_at = clock_type::now();
      const auto read_result =
          slice.take(*source, {.offset = offset, .size = count}, chunk, deferred_reads,
                     std::exchange(io_wait, std::chrono::milliseconds{}));
      record_duration(read_at, timings.maximum_read_ms);
      if (read_result == GNEISS_ERROR_NOT_READY) {
        return GNEISS_ERROR_NOT_READY;
      }
      if (read_result != GNEISS_SUCCESS) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      const auto hash_at = clock_type::now();
      digest.update(chunk);
      record_duration(hash_at, timings.maximum_hash_ms);
      offset += chunk.size();
      byte_budget -= chunk.size();
    }
    return GNEISS_SUCCESS;
  }
  gneiss_result open_source(const std::string& path, const identity& expected,
                            const std::function<bool()>& cancelled, bool& whole_file) {
    const auto opened_at = clock_type::now();
    const auto opened = files.open_read_for_validation("asset://" + path, source);
    record_duration(opened_at, timings.maximum_open_ms);
    whole_file = opened == GNEISS_ERROR_UNSUPPORTED;
    if (whole_file) {
      std::vector<std::byte> bytes;
      if (expected.bytes > std::numeric_limits<std::size_t>::max() ||
          files.read_bounded("asset://" + path, static_cast<std::size_t>(expected.bytes), bytes) !=
              GNEISS_SUCCESS ||
          bytes.size() != expected.bytes || core::sha256(bytes) != expected.digest ||
          (cancelled && cancelled())) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      return GNEISS_SUCCESS;
    }
    if (opened != GNEISS_SUCCESS || source->size() != expected.bytes) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    offset = 0U;
    digest = {};
    return GNEISS_SUCCESS;
  }
  gneiss_result advance(std::size_t byte_budget, const std::function<bool()>& cancelled,
                        bool& complete) {
    // 也限制零长度文件数，字节预算不能使空文件清单变为无界工作。
    std::size_t files_left = 16U;
    while (index < identities.size() && byte_budget != 0U && files_left != 0U) {
      const auto& [path, expected] = identities[index];
      if (!source) {
        bool whole_file{};
        const auto opened = open_source(path, expected, cancelled, whole_file);
        if (opened != GNEISS_SUCCESS) {
          return opened;
        }
        if (whole_file) {
          ++index;
          break;
        }
      }
      if (cancelled && cancelled()) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      const auto read_result = read_next(byte_budget, expected.bytes);
      if (read_result == GNEISS_ERROR_NOT_READY) {
        break;
      }
      if (read_result != GNEISS_SUCCESS) {
        return read_result;
      }
      if (offset == expected.bytes) {
        if (digest.digest() != expected.digest || (cancelled && cancelled()) ||
            source->complete_validation(expected.digest) != GNEISS_SUCCESS) {
          return GNEISS_ERROR_INVALID_STATE;
        }
        source.reset();
        slice = {};
        ++index;
        --files_left;
      }
    }
    complete = index == identities.size();
    return GNEISS_SUCCESS;
  }
};
source_revision_file_system::verification::verification(std::unique_ptr<state> value)
    : state_(std::move(value)) {}
source_revision_file_system::verification::~verification() = default;
std::size_t source_revision_file_system::verification::completed_sources() const noexcept {
  return state_->index;
}
source_revision_file_system::verification::statistics
source_revision_file_system::verification::timings() const noexcept {
  return state_->timings;
}
gneiss_result source_revision_file_system::begin_verification(std::unique_ptr<verification>& output,
                                                              bool deferred_reads) const noexcept {
  output.reset();
  try {
    auto value = std::make_unique<verification::state>();
    value->files = source_;
    value->deferred_reads = deferred_reads;
    {
      const std::scoped_lock lock(versions_->mutex);
      value->identities.assign(versions_->identities.begin(), versions_->identities.end());
    }
    output.reset(new verification(std::move(value)));
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result source_revision_file_system::verification::advance(
    std::size_t byte_budget, const std::function<bool()>& cancelled, bool& complete,
    std::chrono::milliseconds wait_budget) noexcept {
  complete = false;
  auto& value = *state_;
  if (byte_budget == 0U || wait_budget < std::chrono::milliseconds::zero()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  if (value.result != GNEISS_SUCCESS) {
    return value.result;
  }
  try {
    value.io_wait = wait_budget;
    value.result = cancelled && cancelled() ? GNEISS_ERROR_INVALID_STATE
                                            : value.advance(byte_budget, cancelled, complete);
  } catch (const std::bad_alloc&) {
    value.result = GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    value.result = GNEISS_ERROR_INTERNAL;
  }
  if (value.result != GNEISS_SUCCESS) {
    value.slice = {};
    value.source.reset();
  }
  return value.result;
}

gneiss_result
source_revision_file_system::verify(const std::function<bool()>& cancelled) const noexcept {
  std::unique_ptr<verification> cursor;
  auto result = begin_verification(cursor, false);
  bool complete{};
  while (result == GNEISS_SUCCESS && !complete) {
    result = cursor->advance(std::size_t{4U} * 1024U * 1024U, cancelled, complete);
  }
  return result;
}
std::size_t source_revision_file_system::source_count() const {
  const std::scoped_lock lock(versions_->mutex);
  return versions_->identities.size();
}

} // namespace gneiss::asset_internal
