// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/platform/files/native_file_backend.hpp"
#include "engine/platform/files/file_path.hpp"
#include "engine/platform/io/uv/uv_loop_access.hpp"
#include "engine/platform/io/uv/uv_result.hpp"
#include <algorithm>
#include <charconv>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <utility>

namespace gneiss::engine::files {
namespace {
enum class operation_kind { read, write, stat, list };
enum class stage {
  root,
  target,
  parent,
  open,
  info,
  transfer,
  sync,
  close,
  publish,
  cleanup,
  directory
};
struct operation : std::enable_shared_from_this<operation> {
  uv_fs_t request{};
  uv_loop_t* loop{};
  std::shared_ptr<operation> keep_alive;
  operation_kind kind{};
  stage step{};
  std::string root, relative, path, canonical_root, temporary;
  files::bytes bytes;
  range range{};
  std::size_t limit{}, position{}, skip{};
  bool ranged{}, create_new{}, temporary_created{};
  int descriptor{-1};
  result status{result::success};
  files::callback<files::bytes> on_read;
  completion_callback on_write;
  files::callback<files::info> on_stat;
  files::callback<directory_page> on_list;
  files::info info;
  directory_page page;

  static void completed(uv_fs_t* req) noexcept {
    auto* self = static_cast<operation*>(req->data);
    auto owner = self->keep_alive;
    self->advance();
  }
  void submitted(int code) noexcept {
    if (code < 0) {
      if (step == stage::close) {
        // 仅异步关闭提交失败时，在 I/O 线程释放已打开描述符；不向调用方提供同步接口。
        uv_fs_req_cleanup(&request);
        uv_fs_t cleanup{};
        (void)uv_fs_close(nullptr, &cleanup, descriptor, nullptr);
        uv_fs_req_cleanup(&cleanup);
        descriptor = -1;
        finish(status.failed() ? status : from_uv_status(code));
        return;
      }
      request.result = code;
      advance();
    }
  }
  void finish(result value) noexcept {
    if (temporary_created) {
      status = value;
      temporary_created = false;
      step = stage::cleanup;
      submitted(uv_fs_unlink(loop, &request, temporary.c_str(), completed));
      return;
    }
    auto owner = std::move(keep_alive);
    // 后端回调仅进入服务的完成队列；业务回调由所属线程分发。
    if (on_read)
      on_read({value, value.ok() ? std::move(bytes) : files::bytes{}});
    else if (on_write)
      on_write(value);
    else if (on_stat)
      on_stat({value, value.ok() ? info : files::info{}});
    else if (on_list)
      on_list({value, value.ok() ? std::move(page) : directory_page{}});
  }
  void close(result value) noexcept {
    status = value;
    if (descriptor < 0) {
      finish(status);
      return;
    }
    step = stage::close;
    submitted(uv_fs_close(loop, &request, descriptor, completed));
  }
  bool contained(std::string_view value) const noexcept {
    return value == canonical_root ||
           (value.starts_with(canonical_root) &&
            (canonical_root.ends_with('/') || value[canonical_root.size()] == '/'));
  }
  static std::string normalized(const char* value) {
    std::string text(value);
    std::replace(text.begin(), text.end(), '\\', '/');
    while (text.size() > 1 && text.ends_with('/'))
      text.pop_back();
    return text;
  }
  void resolve_target() {
    path = canonical_root + (canonical_root.ends_with('/') ? "" : "/") + relative;
    step = stage::target;
    submitted(uv_fs_realpath(loop, &request, path.c_str(), completed));
  }
  void perform() {
    if (kind == operation_kind::stat) {
      step = stage::info;
      submitted(uv_fs_stat(loop, &request, path.c_str(), completed));
    } else if (kind == operation_kind::list) {
      step = stage::directory;
      submitted(uv_fs_scandir(loop, &request, path.c_str(), 0, completed));
    } else {
      step = stage::open;
      if (kind == operation_kind::write) {
        // 同目录独占临时文件；完整写入、fsync、关闭后才发布目标。
        temporary = path + ".gneiss-XXXXXX";
        submitted(uv_fs_mkstemp(loop, &request, temporary.c_str(), completed));
      } else {
        submitted(uv_fs_open(loop, &request, path.c_str(), O_RDONLY, 0666, completed));
      }
    }
  }
  void transfer() {
    if (position == bytes.size()) {
      if (kind == operation_kind::write) {
        step = stage::sync;
        submitted(uv_fs_fsync(loop, &request, descriptor, completed));
      } else
        close(result::success);
      return;
    }
    step = stage::transfer;
    const auto count = std::min<std::size_t>(bytes.size() - position, 1024U * 1024U);
    auto buffer = uv_buf_init(reinterpret_cast<char*>(bytes.data() + position),
                              static_cast<unsigned int>(count));
    const auto offset = static_cast<std::int64_t>(range.offset + position);
    submitted(kind == operation_kind::read
                  ? uv_fs_read(loop, &request, descriptor, &buffer, 1, offset, completed)
                  : uv_fs_write(loop, &request, descriptor, &buffer, 1, offset, completed));
  }
  void advance() noexcept {
    const auto value = request.result;
    try {
      if (step == stage::directory && value >= 0) {
        uv_dirent_t entry{};
        std::size_t index{};
        int code{};
        while ((code = uv_fs_scandir_next(&request, &entry)) == 0) {
          if (index++ < skip)
            continue;
          if (page.entries.size() == limit) {
            page.next_cursor = std::to_string(skip + page.entries.size());
            break;
          }
          if (entry.type != UV_DIRENT_DIR && entry.type != UV_DIRENT_FILE) {
            uv_fs_req_cleanup(&request);
            finish(result::unsupported);
            return;
          }
          page.entries.push_back(
              {entry.name, {entry.type == UV_DIRENT_DIR ? kind::directory : kind::regular, {}}});
        }
        uv_fs_req_cleanup(&request);
        finish(code != 0 && code != UV_EOF ? from_uv_status(code) : result::success);
        return;
      }
      std::string resolved;
      if ((step == stage::root || step == stage::target || step == stage::parent) && value >= 0)
        resolved = normalized(static_cast<const char*>(request.ptr));
      if (step == stage::open && kind == operation_kind::write && value >= 0) {
        temporary = request.path;
        temporary_created = true;
      }
      const auto metadata = request.statbuf;
      uv_fs_req_cleanup(&request);
      if (step == stage::close) {
        descriptor = -1;
        if (status.ok() && value >= 0 && kind == operation_kind::write) {
          step = stage::publish;
          submitted(create_new
                        ? uv_fs_link(loop, &request, temporary.c_str(), path.c_str(), completed)
                        : uv_fs_rename(loop, &request, temporary.c_str(), path.c_str(), completed));
        } else {
          finish(status.failed() ? status : from_uv_status(static_cast<int>(value)));
        }
        return;
      }
      if (step == stage::cleanup) {
        // 发布结果不因残留临时文件清理失败而改写。
        finish(status);
        return;
      }
      if (step == stage::publish) {
        if (value >= 0 && !create_new)
          temporary_created = false;
        finish(from_uv_status(static_cast<int>(value)));
        return;
      }
      if (value < 0) {
        if (step == stage::target && kind == operation_kind::write && value == UV_ENOENT) {
          const auto slash = path.find_last_of('/');
          const auto parent = path.substr(0, slash);
          step = stage::parent;
          submitted(uv_fs_realpath(loop, &request, parent.c_str(), completed));
          return;
        }
        close(from_uv_status(static_cast<int>(value)));
        return;
      }
      switch (step) {
      case stage::root:
        canonical_root = std::move(resolved);
        resolve_target();
        break;
      case stage::target:
      case stage::parent:
        if (!contained(resolved) || (kind == operation_kind::write && resolved == canonical_root &&
                                     step == stage::target)) {
          close(result::invalid_argument);
          break;
        }
        if (step == stage::parent)
          path = resolved + "/" + path.substr(path.find_last_of('/') + 1);
        else
          path = std::move(resolved);
        perform();
        break;
      case stage::open:
        descriptor = static_cast<int>(value);
        step = stage::info;
        submitted(uv_fs_fstat(loop, &request, descriptor, completed));
        break;
      case stage::info: {
        const auto mode = metadata.st_mode & S_IFMT;
        if (mode != S_IFREG && mode != S_IFDIR) {
          close(result::unsupported);
          break;
        }
        info = {mode == S_IFDIR ? kind::directory : kind::regular, metadata.st_size};
        if (kind == operation_kind::stat) {
          finish(result::success);
          break;
        }
        if (mode != S_IFREG) {
          close(result::invalid_argument);
          break;
        }
        if (kind == operation_kind::read) {
          if (range.offset > metadata.st_size ||
              (ranged && range.size > metadata.st_size - range.offset)) {
            close(result::invalid_argument);
            break;
          }
          const auto count = ranged ? range.size : metadata.st_size;
          if (count > limit ||
              count > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) -
                          range.offset) {
            close(result::invalid_argument);
            break;
          }
          bytes.resize(static_cast<std::size_t>(count));
        }
        transfer();
        break;
      }
      case stage::transfer:
        if (value == 0) {
          close(result::io);
          break;
        }
        position += static_cast<std::size_t>(value);
        transfer();
        break;
      case stage::sync:
        close(result::success);
        break;
      default:
        close(result::internal);
        break;
      }
    } catch (const std::bad_alloc&) {
      uv_fs_req_cleanup(&request);
      close(result::out_of_memory);
    } catch (...) {
      uv_fs_req_cleanup(&request);
      close(result::internal);
    }
  }
  void begin(uv_loop_t* target) noexcept {
    loop = target;
    keep_alive = shared_from_this();
    request.data = this;
    step = stage::root;
    submitted(uv_fs_realpath(loop, &request, root.c_str(), completed));
  }
};
}
struct native_backend::implementation {
  uv_loop_executor& executor;
  std::string root;
  result submit(std::string_view path, std::shared_ptr<operation> pending) {
    if (!detail::valid_file_path(path))
      return result::invalid_argument;
    pending->root = root;
    pending->relative = path;
    return uv_loop_access::post(
        executor, [pending = std::move(pending)](uv_loop_t* loop) { pending->begin(loop); });
  }
};
native_backend::native_backend(uv_loop_executor& executor, std::string root)
    : impl_(std::make_unique<implementation>(implementation{executor, std::move(root)})) {
  if (impl_->root.empty() || impl_->root.find('\0') != std::string::npos)
    throw std::invalid_argument("文件根目录无效");
}
native_backend::~native_backend() = default;
result native_backend::read(std::string_view path, read_options options,
                            files::callback<files::bytes> callback) {
  if (!callback || options.max_bytes == 0)
    return result::invalid_argument;
  auto pending = std::make_shared<operation>();
  pending->kind = operation_kind::read;
  pending->limit = options.max_bytes;
  pending->on_read = std::move(callback);
  return impl_->submit(path, std::move(pending));
}
result native_backend::read_range(std::string_view path, range range,
                                  files::callback<files::bytes> callback) {
  if (!callback ||
      range.offset > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) ||
      range.size >
          static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) - range.offset)
    return result::invalid_argument;
  auto pending = std::make_shared<operation>();
  pending->kind = operation_kind::read;
  pending->ranged = true;
  pending->range = range;
  pending->limit = range.size;
  pending->on_read = std::move(callback);
  return impl_->submit(path, std::move(pending));
}
result native_backend::write(std::string_view path, files::bytes bytes, write_options options,
                             completion_callback callback) {
  if (!callback || path.empty() || path.ends_with('/') ||
      (options.mode != write_mode::replace && options.mode != write_mode::create_new))
    return result::invalid_argument;
  auto pending = std::make_shared<operation>();
  pending->kind = operation_kind::write;
  pending->bytes = std::move(bytes);
  pending->create_new = options.mode == write_mode::create_new;
  pending->on_write = std::move(callback);
  return impl_->submit(path, std::move(pending));
}
result native_backend::stat(std::string_view path, files::callback<files::info> callback) {
  if (!callback)
    return result::invalid_argument;
  auto pending = std::make_shared<operation>();
  pending->kind = operation_kind::stat;
  pending->on_stat = std::move(callback);
  return impl_->submit(path, std::move(pending));
}
result native_backend::list(std::string_view path, list_options options,
                            files::callback<directory_page> callback) {
  if (!callback || options.max_entries == 0)
    return result::invalid_argument;
  auto pending = std::make_shared<operation>();
  pending->kind = operation_kind::list;
  if (!options.cursor.empty()) {
    const auto parsed = std::from_chars(
        options.cursor.data(), options.cursor.data() + options.cursor.size(), pending->skip);
    if (parsed.ec != std::errc{} || parsed.ptr != options.cursor.data() + options.cursor.size())
      return result::invalid_argument;
  }
  pending->limit = options.max_entries;
  pending->on_list = std::move(callback);
  return impl_->submit(path, std::move(pending));
}
result native_backend::flush(completion_callback callback) {
  if (!callback)
    return result::invalid_argument;
  // 每个已完成写入均已 fsync；不承诺尚未完成写入或跨文件事务。
  return impl_->executor.post([callback = std::move(callback)] { callback(result::success); });
}
}
