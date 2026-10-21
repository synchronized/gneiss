// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "caors_vfs_bridge.hpp"
#include <algorithm>
#include <atomic>
#include <gneiss/engine/core/result.hpp>
#include <mutex>
#include <stdexcept>
#include <vector>
namespace gneiss::asset_internal {
namespace {
inline caors::io::io_error translate(gneiss_result code) {
  const result status{code};
  using caors::io::io_errc;

  if (status == result::success)
    return {};
  if (status == result::unsupported)
    return {io_errc::unsupported};
  if (status == result::not_found)
    return {io_errc::not_found};
  if (status == result::invalid_state)
    return {io_errc::closed};
  if (status == result::invalid_argument)
    return {io_errc::invalid_argument};
  if (status == result::out_of_memory)
    return {io_errc::out_of_memory};
  if (status == result::not_ready)
    return {io_errc::pending};

  return {io_errc::backend_failure, 1, static_cast<gneiss_result>(status)};
}

// 调度式读取只产生内存快照；底层句柄生命周期仍由 read_bounded 的后端负责。
// 关闭快照阻止新读，已接受的读请求独立保活字节，不冒充操作系统异步关闭。
class snapshot_source final : public read_source {
  class operation final : public read_operation {
  public:
    explicit operation(std::shared_ptr<const std::vector<std::byte>> bytes)
        : bytes_(std::move(bytes)) {}
    gneiss_result poll(std::span<const std::byte>& output) noexcept override {
      output = *bytes_;
      return GNEISS_SUCCESS;
    }

  private:
    std::shared_ptr<const std::vector<std::byte>> bytes_;
  };

public:
  explicit snapshot_source(std::vector<std::byte> bytes)
      : size_(bytes.size()),
        bytes_(std::make_shared<const std::vector<std::byte>>(std::move(bytes))) {}
  std::uint64_t size() const noexcept override { return size_; }
  gneiss_result begin_close() const noexcept override {
    const std::scoped_lock lock(mutex_);
    bytes_.reset();
    return GNEISS_SUCCESS;
  }
  gneiss_result poll_close() const noexcept override {
    const std::scoped_lock lock(mutex_);
    return bytes_ ? GNEISS_ERROR_NOT_READY : GNEISS_SUCCESS;
  }
  gneiss_result read_at(std::uint64_t offset, std::span<std::byte> output) const noexcept override {
    const std::scoped_lock lock(mutex_);
    if (!bytes_)
      return GNEISS_ERROR_INVALID_STATE;
    if (offset > size_ || output.size() > size_ - offset)
      return GNEISS_ERROR_INVALID_ARGUMENT;
    std::copy_n(bytes_->begin() + static_cast<std::ptrdiff_t>(offset), output.size(),
                output.begin());
    return GNEISS_SUCCESS;
  }
  gneiss_result begin_read(std::uint64_t offset, std::size_t length,
                           std::unique_ptr<read_operation>& output) const noexcept override {
    output.reset();
    const std::scoped_lock lock(mutex_);
    if (!bytes_)
      return GNEISS_ERROR_INVALID_STATE;
    if (offset != 0 || length != size_)
      return GNEISS_ERROR_UNSUPPORTED;
    try {
      output = std::make_unique<operation>(bytes_);
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    return GNEISS_SUCCESS;
  }

private:
  std::uint64_t size_;
  mutable std::mutex mutex_;
  mutable std::shared_ptr<const std::vector<std::byte>> bytes_;
};

class scheduled_open final : public open_read_operation {
public:
  struct state {
    std::mutex mutex;
    gneiss_result status = GNEISS_ERROR_NOT_READY;
    std::unique_ptr<read_source> source;
    bool cancelled = false;
  };
  // 排队任务被执行器丢弃也必须结束回执，不能永久停留在 NOT_READY。
  struct settlement {
    std::shared_ptr<state> value;
    ~settlement() {
      const std::scoped_lock lock(value->mutex);
      if (value->status == GNEISS_ERROR_NOT_READY)
        value->status = GNEISS_ERROR_INVALID_STATE;
    }
  };
  explicit scheduled_open(std::shared_ptr<state> value) : state_(std::move(value)) {}
  ~scheduled_open() override {
    const std::scoped_lock lock(state_->mutex);
    state_->cancelled = true;
  }
  gneiss_result poll(std::unique_ptr<read_source>& output) noexcept override {
    output.reset();
    const std::scoped_lock lock(state_->mutex);
    if (state_->status != GNEISS_SUCCESS)
      return state_->status;
    output = std::move(state_->source);
    return output ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE;
  }

private:
  std::shared_ptr<state> state_;
};

class async_file final : public caors_internal::file_adapter {
  struct read {
    std::unique_ptr<read_operation> operation;
    caors::memory_resource* resource{};
    caors::io::io_completion<caors::io::byte_buffer> completion;
  };

public:
  async_file(std::shared_ptr<read_source> file, caors::tasks::executor worker, std::size_t limit)
      : file_(std::move(file)), worker_(std::move(worker)), maximum_document_bytes_(limit) {}
  ~async_file() override { (void)file_->begin_close(); }
  // 宿主放弃 pump 前必须调用；解除凭证保活环，真实 I/O 仍由 libuv 排空。
  void abandon() override {
    std::vector<read> reads;
    std::vector<caors::io::io_completion<caors::io::close_result>> closes;
    {
      const std::scoped_lock lock(mutex_);
      closing_ = true;
      reads.swap(reads_);
      closes.swap(closes_);
      (void)file_->begin_close();
    }
  }
  std::uint64_t size() const noexcept override { return file_->size(); }
  std::uint64_t max_read_size() const noexcept override { return maximum_document_bytes_; }
  caors::io::io_error
  start_read(std::uint64_t offset, std::uint64_t length, caors::memory_resource* resource,
             caors::io::io_completion<caors::io::byte_buffer> completion) override {
    using caors::io::io_errc;
    const std::scoped_lock lock(mutex_);
    if (closing_)
      return {io_errc::closed};
    if (offset > size() || length > size() - offset)
      return {io_errc::out_of_range};
    if (length > max_read_size())
      return {io_errc::limit_exceeded};
    if (offset != 0 || length != size())
      return {io_errc::unsupported}; // 首阶段仅桥接需要完整摘要的 CPU 文档。
    // 先保留容器容量，保证底层接受后不再出现入队分配失败。
    reads_.reserve(reads_.size() + 1);
    read pending;
    const auto status =
        file_->begin_read(offset, static_cast<std::size_t>(length), pending.operation);
    if (status != GNEISS_SUCCESS)
      return translate(status);
    pending.resource = resource;
    pending.completion = std::move(completion);
    reads_.push_back(std::move(pending));
    return {};
  }
  caors::io::io_error
  start_close(caors::io::io_completion<caors::io::close_result> completion) override {
    const std::scoped_lock lock(mutex_);
    closes_.reserve(closes_.size() + 1);
    const auto started = file_->begin_close();
    if (started != GNEISS_SUCCESS)
      return translate(started);
    closes_.push_back(std::move(completion));
    closing_ = true;
    return {};
  }
  void pump() override {
    std::vector<read> ready;
    std::vector<caors::io::io_completion<caors::io::close_result>> closed;
    gneiss_result close_status = GNEISS_ERROR_NOT_READY;
    {
      const std::scoped_lock lock(mutex_);
      ready.reserve(reads_.size());
      for (auto it = reads_.begin(); it != reads_.end();) {
        std::span<const std::byte> bytes;
        if (it->operation->poll(bytes) == GNEISS_ERROR_NOT_READY) {
          ++it;
          continue;
        }
        ready.push_back(std::move(*it));
        it = reads_.erase(it);
      }
      close_status = file_->poll_close();
      if (closing_ && close_status != GNEISS_ERROR_NOT_READY && reads_.empty() && ready.empty() &&
          processing_->load(std::memory_order_acquire) == 0)
        closed.swap(closes_);
    }
    for (auto& pending : ready) {
      // 移交读取回执而非复制负载；任务保活来源和回执，字节一直有效。
      auto job = std::make_shared<processing_read>(std::move(pending), file_, processing_);
      const auto posted = worker_.post(caors::tasks::task{[job] {
        using namespace caors::io;
        std::span<const std::byte> bytes;
        auto status = translate(job->pending.operation->poll(bytes));
        auto output = make_byte_buffer(job->pending.resource);
        if (job->pending.completion.stop_token().stop_requested())
          status = {io_errc::cancelled};
        if (!status) {
          try {
            status = translate(job->source->complete_validation(core::sha256(bytes)));
            if (!status)
              output.assign(bytes.begin(), bytes.end());
          } catch (const std::bad_alloc&) {
            status = {io_errc::out_of_memory};
          } catch (...) {
            status = {io_errc::backend_failure};
          }
        }
        if (job->pending.completion.stop_token().stop_requested())
          status = {io_errc::cancelled};
        (void)job->pending.completion.complete({status, std::move(output)});
      }});
      if (posted)
        (void)job->pending.completion.complete({{caors::io::io_errc::backend_failure}, {}});
    }
    for (auto& completion : closed)
      (void)completion.complete({translate(close_status), {}});
  }

private:
  // 任务执行、投递失败和队列丢弃都释放计数，关闭回执不会越过后台处理。
  struct processing_read {
    read pending;
    std::shared_ptr<read_source> source;
    std::shared_ptr<std::atomic_size_t> active;
    processing_read(read value, std::shared_ptr<read_source> file,
                    std::shared_ptr<std::atomic_size_t> count)
        : pending(std::move(value)), source(std::move(file)), active(std::move(count)) {
      active->fetch_add(1, std::memory_order_relaxed);
    }
    ~processing_read() { active->fetch_sub(1, std::memory_order_release); }
  };
  std::shared_ptr<read_source> file_;
  caors::tasks::executor worker_;
  std::size_t maximum_document_bytes_;
  std::shared_ptr<std::atomic_size_t> processing_{std::make_shared<std::atomic_size_t>(0)};
  std::mutex mutex_;
  bool closing_{};
  std::vector<read> reads_;
  std::vector<caors::io::io_completion<caors::io::close_result>> closes_;
};
class vfs_backend final : public caors_vfs_bridge {
  struct opening {
    std::unique_ptr<open_read_operation> operation;
    caors::io::io_completion<std::shared_ptr<caors::io::async_file>> completion;
  };

public:
  vfs_backend(virtual_file_system files, caors::tasks::executor worker, std::size_t limit,
              bool scheduled)
      : files_snapshot_(std::move(files)), worker_(std::move(worker)),
        maximum_document_bytes_(limit), allow_scheduled_reads_(scheduled) {}
  bool exists(caors::io::path_view) const override { return false; } // 不提供同步状态查询。
  gneiss_result identify(std::string_view uri, std::uint32_t type,
                         asset_identity& output) const noexcept override {
    return files_snapshot_.identify(uri, type, output);
  }
  caors::io::byte_buffer_result read_all(caors::io::path_view, caors::memory_resource*) override {
    return {{caors::error_domain::io, caors::errc::unsupported}, {}};
  }
  caors::error write_all(caors::io::path_view, std::span<const std::byte>) override {
    return {caors::error_domain::io, caors::errc::unsupported};
  }
  caors::io::file_system_capabilities capabilities() const noexcept override {
    return {.read = true};
  }
  caors::io::io_error
  start_open(caors::io::resolved_file location, caors::memory_resource*,
             caors::io::io_completion<std::shared_ptr<caors::io::async_file>> completion) override {
    const std::scoped_lock lock(mutex_);
    if (stopped_)
      return {caors::io::io_errc::closed};
    if (location.backend.get() != this)
      return {caors::io::io_errc::invalid_argument};
    pending_.reserve(pending_.size() + 1);
    opening next;
    auto status =
        files_snapshot_.begin_open_read_for_validation(location.path.string_view(), next.operation);
    if (status == GNEISS_ERROR_UNSUPPORTED && allow_scheduled_reads_) {
      auto state = std::make_shared<scheduled_open::state>();
      next.operation = std::make_unique<scheduled_open>(state);
      auto settlement = std::make_shared<scheduled_open::settlement>();
      settlement->value = state;
      const auto posted = worker_.post(caors::tasks::task{
          [state, settlement, files = files_snapshot_,
           path = std::string(location.path.string_view()), limit = maximum_document_bytes_] {
            {
              const std::scoped_lock guard(state->mutex);
              if (state->cancelled)
                return;
            }
            std::vector<std::byte> bytes;
            auto result = files.read_bounded(path, limit, bytes);
            std::unique_ptr<read_source> source;
            if (result == GNEISS_SUCCESS) {
              try {
                source = std::make_unique<snapshot_source>(std::move(bytes));
              } catch (const std::bad_alloc&) {
                result = GNEISS_ERROR_OUT_OF_MEMORY;
              }
            }
            const std::scoped_lock guard(state->mutex);
            if (!state->cancelled) {
              state->source = std::move(source);
              state->status = result;
            }
          }});
      if (posted)
        return {caors::io::io_errc::backend_failure};
      status = GNEISS_SUCCESS;
    }
    if (status != GNEISS_SUCCESS)
      return translate(status);
    next.completion = std::move(completion);
    pending_.push_back(std::move(next));
    return {};
  }
  void pump() override {
    std::vector<opening> ready;
    {
      const std::scoped_lock lock(mutex_);
      // 单消费者打开回执不能预先 poll；先取出，再将未就绪项放回。
      ready.swap(pending_);
    }
    for (auto& next : ready) {
      if (next.completion.stop_token().stop_requested()) {
        (void)next.completion.complete({{caors::io::io_errc::cancelled}, {}});
        continue;
      }
      std::unique_ptr<read_source> source;
      const auto status = next.operation->poll(source);
      if (status == GNEISS_ERROR_NOT_READY) {
        const std::scoped_lock lock(mutex_);
        pending_.push_back(std::move(next));
        continue;
      }
      if (status != GNEISS_SUCCESS) {
        (void)next.completion.complete({translate(status), {}});
        continue;
      }
      try {
        auto file =
            std::make_shared<async_file>(std::move(source), worker_, maximum_document_bytes_);
        files_.push_back(file);
        (void)next.completion.complete({{}, std::move(file)});
      } catch (const std::bad_alloc&) {
        (void)next.completion.complete({{caors::io::io_errc::out_of_memory}, {}});
      }
    }
    for (auto it = files_.begin(); it != files_.end();) {
      if (auto file = it->lock()) {
        file->pump();
        ++it;
      } else
        it = files_.erase(it);
    }
  }
  // 不再接收打开；释放所有未交付凭证，loop 继续排空真实操作。
  void abandon() override {
    std::vector<opening> pending;
    {
      const std::scoped_lock lock(mutex_);
      stopped_ = true;
      pending.swap(pending_);
    }
    for (auto& weak : files_)
      if (auto file = weak.lock())
        file->abandon();
    files_.clear();
  }

private:
  virtual_file_system files_snapshot_;
  caors::tasks::executor worker_;
  std::size_t maximum_document_bytes_;
  std::mutex mutex_;
  bool stopped_{};
  bool allow_scheduled_reads_{};
  std::vector<opening> pending_;
  std::vector<std::weak_ptr<caors_internal::file_adapter>> files_;
};
}
std::shared_ptr<caors_vfs_bridge> make_caors_vfs_bridge(virtual_file_system snapshot,
                                                        caors::tasks::executor worker,
                                                        std::size_t maximum_document_bytes,
                                                        bool allow_scheduled_reads) {
  if (maximum_document_bytes == 0U)
    throw std::invalid_argument("文档读取上限不得为零");
  return std::make_shared<vfs_backend>(std::move(snapshot), std::move(worker),
                                       maximum_document_bytes, allow_scheduled_reads);
}
}
