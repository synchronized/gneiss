// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/vfs/source_revision_file_system.hpp"
#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include "engine/resource/vfs/read_slice.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>

namespace gneiss::engine::resource {
namespace {
using clock_type = std::chrono::steady_clock;
void record_duration(clock_type::time_point start, double& maximum) {
  maximum = std::max(maximum,
                     std::chrono::duration<double, std::milli>(clock_type::now() - start).count());
}
gneiss::engine::core::result hash_source(const read_source& source,
                                         engine::core::sha256_digest& digest,
                                         const std::function<bool()>& cancelled) {
  std::array<std::byte, std::size_t{64U} * 1024U> buffer{};
  engine::core::sha256_builder builder;
  for (std::uint64_t offset = 0U; offset < source.size();) {
    if (cancelled && cancelled()) {
      return gneiss::engine::core::result::invalid_state;
    }
    const auto count =
        static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), source.size() - offset));
    const auto chunk = std::span{buffer}.first(count);
    const auto result = source.read_at(offset, chunk);
    if (result != gneiss::engine::core::result::success) {
      return result;
    }
    builder.update(chunk);
    offset += count;
  }
  if (cancelled && cancelled()) {
    return gneiss::engine::core::result::invalid_state;
  }
  digest = builder.digest();
  return gneiss::engine::core::result::success;
}
} // namespace

gneiss::engine::core::result
source_revision_file_system::identify_source(std::string_view path, std::uint32_t type,
                                             asset_identity& output) const noexcept {
  output = {};
  try {
    return source_.identify("asset://" + std::string(path), type, output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
source_revision_file_system::observed_revision(std::string_view path,
                                               source_revision& output) const noexcept {
  output = {};
  try {
    if (validate_uri("asset://" + std::string(path)) != gneiss::engine::core::result::success) {
      return gneiss::engine::core::result::invalid_argument;
    }
    const std::scoped_lock lock(versions_->mutex);
    const auto found = versions_->identities.find(std::string(path));
    if (found == versions_->identities.end()) {
      return gneiss::engine::core::result::not_ready;
    }
    output = found->second;
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
source_revision_file_system::remember(const std::shared_ptr<ledger>& versions,
                                      std::string_view path, const identity& current) {
  const std::scoped_lock lock(versions->mutex);
  const auto found = versions->identities.find(std::string(path));
  if (found != versions->identities.end()) {
    return found->second.bytes == current.bytes && found->second.digest == current.digest
               ? gneiss::engine::core::result::success
               : gneiss::engine::core::result::invalid_state;
  }
  if (versions->identities.size() >= 65536U) {
    return gneiss::engine::core::result::invalid_argument;
  }
  versions->identities.emplace(path, current);
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result
source_revision_file_system::open_read(std::string_view path,
                                       std::unique_ptr<read_source>& output) const noexcept {
  output.reset();
  try {
    std::unique_ptr<read_source> source;
    auto result = source_.open_read("asset://" + std::string(path), source);
    if (result != gneiss::engine::core::result::success) {
      return result;
    }
    identity current{.bytes = source->size(), .digest = {}};
    result = hash_source(*source, current.digest, cancelled_);
    if (result == gneiss::engine::core::result::success) {
      result = source->complete_validation(current.digest);
      if (result == gneiss::engine::core::result::success) {
        result = remember(versions_, path, current);
      }
    }
    if (result == gneiss::engine::core::result::success) {
      output = std::move(source);
    }
    return result;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

namespace {
class tracked_read_source final : public read_source {
public:
  using commit_function =
      std::function<gneiss::engine::core::result(const engine::core::sha256_digest&)>;
  tracked_read_source(std::unique_ptr<read_source> source, commit_function commit)
      : source_(std::move(source)), commit_(std::move(commit)) {}
  gneiss::engine::core::result begin_close() const noexcept override {
    return source_->begin_close();
  }
  gneiss::engine::core::result poll_close() const noexcept override {
    return source_->poll_close();
  }
  [[nodiscard]] gneiss::engine::core::result
  begin_read(std::uint64_t offset, std::size_t size,
             std::unique_ptr<read_operation>& output) const noexcept override {
    return source_->begin_read(offset, size, output);
  }
  [[nodiscard]] std::uint64_t size() const noexcept override { return source_->size(); }
  [[nodiscard]] gneiss::engine::core::result
  read_at(std::uint64_t offset, std::span<std::byte> output) const noexcept override {
    return source_->read_at(offset, output);
  }
  [[nodiscard]] gneiss::engine::core::result
  complete_validation(const engine::core::sha256_digest& digest) const noexcept override {
    try {
      const auto result = source_->complete_validation(digest);
      return result == gneiss::engine::core::result::success ? commit_(digest) : result;
    } catch (const std::bad_alloc&) {
      return gneiss::engine::core::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::core::result::internal;
    }
  }

private:
  std::unique_ptr<read_source> source_;
  commit_function commit_;
};
} // namespace

gneiss::engine::core::result source_revision_file_system::open_read_for_validation(
    std::string_view path, std::unique_ptr<read_source>& output) const noexcept {
  output.reset();
  try {
    std::unique_ptr<read_source> source;
    const auto result = source_.open_read_for_validation("asset://" + std::string(path), source);
    if (result != gneiss::engine::core::result::success) {
      return result;
    }
    const auto bytes = source->size();
    output = std::make_unique<tracked_read_source>(
        std::move(source),
        [versions = versions_, path = std::string(path), bytes](const auto& digest) {
          return remember(versions, path, {.bytes = bytes, .digest = digest});
        });
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result source_revision_file_system::begin_open_read_for_validation(
    std::string_view path, std::unique_ptr<open_read_operation>& output) const noexcept {
  output.reset();
  try {
    class tracked_open final : public open_read_operation {
    public:
      using commit = std::function<gneiss::engine::core::result(
          std::uint64_t, const engine::core::sha256_digest&)>;
      tracked_open(std::unique_ptr<open_read_operation> pending, commit record)
          : pending_(std::move(pending)), record_(std::move(record)) {}
      gneiss::engine::core::result
      poll(std::unique_ptr<read_source>& destination) noexcept override {
        destination.reset();
        if (!pending_) {
          return gneiss::engine::core::result::invalid_state;
        }
        if (result_ != gneiss::engine::core::result::success) {
          return result_;
        }
        try {
          std::unique_ptr<read_source> source;
          const auto received = pending_->poll(source);
          if (received == gneiss::engine::core::result::not_ready) {
            return received;
          }
          if (received != gneiss::engine::core::result::success || !source) {
            result_ = received == gneiss::engine::core::result::success
                          ? gneiss::engine::core::result::internal
                          : received;
            return result_;
          }
          const auto bytes = source->size();
          destination = std::make_unique<tracked_read_source>(
              std::move(source),
              [record = record_, bytes](const auto& digest) { return record(bytes, digest); });
          pending_.reset();
          return gneiss::engine::core::result::success;
        } catch (const std::bad_alloc&) {
          result_ = gneiss::engine::core::result::out_of_memory;
        } catch (...) {
          result_ = gneiss::engine::core::result::internal;
        }
        return result_;
      }

    private:
      std::unique_ptr<open_read_operation> pending_;
      commit record_;
      gneiss::engine::core::result result_{gneiss::engine::core::result::success};
    };
    std::unique_ptr<open_read_operation> pending;
    const auto started =
        source_.begin_open_read_for_validation("asset://" + std::string(path), pending);
    if (started != gneiss::engine::core::result::success) {
      return started;
    }
    if (!pending) {
      return gneiss::engine::core::result::internal;
    }
    output = std::make_unique<tracked_open>(
        std::move(pending),
        [versions = versions_, path = std::string(path)](std::uint64_t bytes, const auto& digest) {
          return remember(versions, path, {.bytes = bytes, .digest = digest});
        });
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
source_revision_file_system::read(std::string_view path,
                                  std::vector<std::byte>& bytes) const noexcept {
  return read_bounded(path, std::size_t{256U} * 1024U * 1024U, bytes);
}
gneiss::engine::core::result
source_revision_file_system::read_bounded(std::string_view path, std::size_t limit,
                                          std::vector<std::byte>& bytes) const noexcept {
  try {
    const auto result = source_.read_bounded("asset://" + std::string(path), limit, bytes);
    if (result != gneiss::engine::core::result::success) {
      return result;
    }
    const identity current{.bytes = bytes.size(), .digest = engine::core::sha256(bytes)};
    const auto recorded = remember(versions_, path, current);
    if (recorded != gneiss::engine::core::result::success) {
      bytes.clear();
    }
    return recorded;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}
struct source_revision_file_system::verification::state {
  virtual_file_system files;
  std::vector<std::pair<std::string, identity>> identities;
  std::size_t index{};
  std::unique_ptr<read_source> source;
  std::unique_ptr<open_read_operation> opening;
  engine::core::sha256_builder digest;
  std::uint64_t offset{};
  gneiss::engine::core::result result{gneiss::engine::core::result::success};
  statistics timings;
  read_slice slice;
  bool deferred_reads{true};
  std::chrono::milliseconds io_wait{};
  gneiss::engine::core::result read_next(std::size_t& byte_budget, std::uint64_t expected_bytes) {
    GNEISS_PROFILE_SCOPE("asset.verify.read_next");
    const auto count =
        static_cast<std::size_t>(std::min<std::uint64_t>(byte_budget, expected_bytes - offset));
    if (count != 0U) {
      std::span<const std::byte> chunk;
      const auto read_at = clock_type::now();
      const auto read_result =
          slice.take(*source, {.offset = offset, .size = count}, chunk, deferred_reads,
                     std::exchange(io_wait, std::chrono::milliseconds{}));
      record_duration(read_at, timings.maximum_read_ms);
      if (read_result == gneiss::engine::core::result::not_ready) {
        return gneiss::engine::core::result::not_ready;
      }
      if (read_result != gneiss::engine::core::result::success) {
        return gneiss::engine::core::result::invalid_state;
      }
      const auto hash_at = clock_type::now();
      {
        GNEISS_PROFILE_SCOPE("asset.verify.hash");
        digest.update(chunk);
      }
      record_duration(hash_at, timings.maximum_hash_ms);
      offset += chunk.size();
      byte_budget -= chunk.size();
    }
    return gneiss::engine::core::result::success;
  }
  gneiss::engine::core::result open_source(const std::string& path, const identity& expected,
                                           const std::function<bool()>& cancelled,
                                           bool& whole_file) {
    GNEISS_PROFILE_SCOPE("asset.verify.open");
    const auto opened_at = clock_type::now();
    auto opened = gneiss::engine::core::result::unsupported;
    bool synchronous_fallback = !deferred_reads;
    if (deferred_reads) {
      if (!opening) {
        opened = files.begin_open_read_for_validation("asset://" + path, opening);
        synchronous_fallback = opened == gneiss::engine::core::result::unsupported;
        if (opened == gneiss::engine::core::result::success && !opening) {
          return gneiss::engine::core::result::internal;
        }
      }
      if (opening) {
        opened = opening->poll(source);
        if (opened != gneiss::engine::core::result::not_ready) {
          opening.reset();
        }
      }
    }
    if (synchronous_fallback) {
      opened = files.open_read_for_validation("asset://" + path, source);
    }
    if (opened == gneiss::engine::core::result::not_ready) {
      record_duration(opened_at, timings.maximum_open_ms);
      return opened;
    }
    record_duration(opened_at, timings.maximum_open_ms);
    whole_file = synchronous_fallback && opened == gneiss::engine::core::result::unsupported;
    if (whole_file) {
      std::vector<std::byte> bytes;
      if (expected.bytes > std::numeric_limits<std::size_t>::max() ||
          files.read_bounded("asset://" + path, static_cast<std::size_t>(expected.bytes), bytes) !=
              gneiss::engine::core::result::success ||
          bytes.size() != expected.bytes || engine::core::sha256(bytes) != expected.digest ||
          (cancelled && cancelled())) {
        return gneiss::engine::core::result::invalid_state;
      }
      return gneiss::engine::core::result::success;
    }
    if (opened != gneiss::engine::core::result::success || !source ||
        source->size() != expected.bytes) {
      return gneiss::engine::core::result::invalid_state;
    }
    offset = 0U;
    digest = {};
    return gneiss::engine::core::result::success;
  }
  gneiss::engine::core::result open_next(const std::string& path, const identity& expected,
                                         const std::function<bool()>& cancelled) {
    bool whole_file{};
    const auto opened = open_source(path, expected, cancelled, whole_file);
    if (opened == gneiss::engine::core::result::success && whole_file) {
      ++index;
      // 旧整文件后端每步最多一个文件，完成后也让出本步。
      return gneiss::engine::core::result::not_ready;
    }
    return opened;
  }
  gneiss::engine::core::result advance(std::size_t byte_budget,
                                       const std::function<bool()>& cancelled, bool& complete) {
    // 也限制零长度文件数，字节预算不能使空文件清单变为无界工作。
    std::size_t files_left = 16U;
    while (index < identities.size() && byte_budget != 0U && files_left != 0U) {
      const auto& [path, expected] = identities[index];
      if (!source) {
        const auto opened = open_next(path, expected, cancelled);
        if (opened == gneiss::engine::core::result::not_ready) {
          break;
        }
        if (opened != gneiss::engine::core::result::success) {
          return opened;
        }
      }
      if (cancelled && cancelled()) {
        return gneiss::engine::core::result::invalid_state;
      }
      const auto read_result = read_next(byte_budget, expected.bytes);
      if (read_result == gneiss::engine::core::result::not_ready) {
        break;
      }
      if (read_result != gneiss::engine::core::result::success) {
        return read_result;
      }
      if (offset == expected.bytes) {
        if (digest.digest() != expected.digest || (cancelled && cancelled()) ||
            source->complete_validation(expected.digest) != gneiss::engine::core::result::success) {
          return gneiss::engine::core::result::invalid_state;
        }
        source.reset();
        slice = {};
        ++index;
        --files_left;
      }
    }
    complete = index == identities.size();
    return gneiss::engine::core::result::success;
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
gneiss::engine::core::result
source_revision_file_system::begin_verification(std::unique_ptr<verification>& output,
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
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}
gneiss::engine::core::result source_revision_file_system::verification::advance(
    std::size_t byte_budget, const std::function<bool()>& cancelled, bool& complete,
    std::chrono::milliseconds wait_budget) noexcept {
  complete = false;
  auto& value = *state_;
  if (byte_budget == 0U || wait_budget < std::chrono::milliseconds::zero()) {
    return gneiss::engine::core::result::invalid_argument;
  }
  if (value.result != gneiss::engine::core::result::success) {
    return value.result;
  }
  try {
    value.io_wait = wait_budget;
    value.result = cancelled && cancelled() ? gneiss::engine::core::result::invalid_state
                                            : value.advance(byte_budget, cancelled, complete);
  } catch (const std::bad_alloc&) {
    value.result = gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    value.result = gneiss::engine::core::result::internal;
  }
  if (value.result != gneiss::engine::core::result::success) {
    value.slice = {};
    value.opening.reset();
    value.source.reset();
  }
  return value.result;
}

gneiss::engine::core::result
source_revision_file_system::verify(const std::function<bool()>& cancelled) const noexcept {
  std::unique_ptr<verification> cursor;
  auto result = begin_verification(cursor, false);
  bool complete{};
  while (result == gneiss::engine::core::result::success && !complete) {
    result = cursor->advance(std::size_t{4U} * 1024U * 1024U, cancelled, complete);
  }
  return result;
}
std::size_t source_revision_file_system::source_count() const {
  const std::scoped_lock lock(versions_->mutex);
  return versions_->identities.size();
}

} // namespace gneiss::engine::resource
