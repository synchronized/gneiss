// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/platform/files/browser/browser_file_backend.hpp"
#include "engine/platform/files/file_path.hpp"
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <dirent.h>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace gneiss::engine {
namespace {
// 仅创建独立内存文件系统，不触发同步磁盘 I/O。
EM_JS(int, mount_files, (const char* root, int persistent), {
  const path = UTF8ToString(root);
  let created = false;
  try {
    FS.mkdir(path);
    created = true;
    FS.mount(persistent ? IDBFS : MEMFS, {}, path);
    return 0;
  } catch (error) {
    if (created) {
      try {
        FS.rmdir(path);
      } catch (_) {
      }
    }
    return -1;
  }
});
EM_JS(void, unmount_files, (const char* root), {
  const path = UTF8ToString(root);
  try {
    FS.unmount(path);
    FS.rmdir(path);
  } catch (_) {
  }
});
using sync_callback = void (*)(void*, int);
EM_JS(void, sync_files, (const char* root, int populate, void* context, sync_callback callback), {
  let completed = false;
  const finish = (error) => {
    if (completed)
      return;
    completed = true;
    getWasmTableEntry(callback)(context, error ? 1 : 0);
  };
  try {
    const mount = FS.lookupPath(UTF8ToString(root), {follow_mount : true}).node.mount;
    // FS.syncfs 会遍历所有挂载；只同步本挂载，避免恢复覆盖其他后端尚未 flush 的写入。
    mount.type.syncfs(mount, !!populate, finish);
  } catch (error) {
    finish(error);
  }
});
result error_result() {
  if (errno == ENOENT)
    return result::not_found;
  if (errno == EEXIST)
    return result::invalid_state;
  if (errno == ENOMEM)
    return result::out_of_memory;
  return result::io;
}
struct descriptor {
  int value{-1};
  ~descriptor() {
    if (value >= 0)
      ::close(value);
  }
};
result information(const struct stat& value, file_info& output) {
  if (S_ISREG(value.st_mode))
    output = {file_kind::regular, static_cast<std::uint64_t>(value.st_size)};
  else if (S_ISDIR(value.st_mode))
    output = {file_kind::directory, {}};
  else
    return result::unsupported;
  return result::success;
}
}
struct browser_file_backend::implementation : std::enable_shared_from_this<implementation> {
  std::string root;
  bool persistent{}, mounted{}, ready{}, busy{};
  ~implementation() {
    if (mounted)
      unmount_files(root.c_str());
  }
  result path(std::string_view relative, std::string& output, bool missing = false) const {
    if (!emscripten_is_main_runtime_thread())
      return result::invalid_state;
    if (!ready || busy)
      return result::not_ready;
    if (!detail::valid_file_path(relative))
      return result::invalid_argument;
    output = root;
    // 拒绝每个路径分量的符号链接，避免内存 FS 中的越界访问。
    while (!relative.empty()) {
      const auto slash = relative.find('/');
      output += '/';
      output += relative.substr(0, slash);
      struct stat info{};
      if (::lstat(output.c_str(), &info) != 0) {
        if (missing && slash == std::string_view::npos && errno == ENOENT)
          return result::success;
        return error_result();
      }
      if (S_ISLNK(info.st_mode))
        return result::invalid_argument;
      if (slash == std::string_view::npos)
        break;
      relative.remove_prefix(slash + 1);
    }
    return result::success;
  }
  struct sync_request {
    std::shared_ptr<implementation> owner;
    completion_callback callback;
    bool populate{};
  };
  static void synced(void* context, int error) {
    std::unique_ptr<sync_request> request(static_cast<sync_request*>(context));
    request->owner->busy = false;
    if (request->populate)
      request->owner->ready = !error;
    request->callback(error ? result::io : result::success);
  }
  result sync(bool populate, completion_callback callback) {
    auto request = std::make_unique<sync_request>(
        sync_request{shared_from_this(), std::move(callback), populate});
    busy = true;
    sync_files(root.c_str(), populate, request.release(), synced);
    return result::success;
  }
  result bytes(std::string_view relative, std::optional<file_range> range, std::size_t limit,
               file_callback<file_bytes> callback) {
    std::string filename;
    auto status = path(relative, filename);
    if (status.failed())
      return status;
    descriptor file{::open(filename.c_str(), O_RDONLY)};
    if (file.value < 0)
      return error_result();
    struct stat info{};
    if (::fstat(file.value, &info) != 0)
      return error_result();
    if (!S_ISREG(info.st_mode) || info.st_size < 0)
      return result::invalid_argument;
    const auto size = static_cast<std::uint64_t>(info.st_size);
    const auto offset = range ? range->offset : 0;
    const auto count = range ? static_cast<std::uint64_t>(range->size) : size;
    if (offset > size || count > size - offset || count > limit ||
        count > static_cast<std::uint64_t>(std::numeric_limits<ssize_t>::max()))
      return result::invalid_argument;
    file_bytes output(static_cast<std::size_t>(count));
    if (count && ::pread(file.value, output.data(), output.size(), static_cast<off_t>(offset)) !=
                     static_cast<ssize_t>(count))
      return result::io;
    callback({result::success, std::move(output)});
    return result::success;
  }
};
browser_file_backend::browser_file_backend(std::string root, bool persistent)
    : impl_(std::make_shared<implementation>()) {
  if (!emscripten_is_main_runtime_thread() || !root.starts_with('/') || root.size() < 2 ||
      root.find('/', 1) != std::string::npos ||
      !detail::valid_file_path(std::string_view(root).substr(1)))
    throw std::invalid_argument("浏览器挂载根目录无效");
  impl_->root = std::move(root);
  impl_->persistent = persistent;
}
browser_file_backend::~browser_file_backend() = default;
result browser_file_backend::initialize(completion_callback callback) {
  if (!callback)
    return result::invalid_argument;
  if (!emscripten_is_main_runtime_thread() || impl_->ready)
    return result::invalid_state;
  if (impl_->busy)
    return result::not_ready;
  if (!impl_->mounted) {
    if (mount_files(impl_->root.c_str(), impl_->persistent) != 0)
      return result::io;
    impl_->mounted = true;
  }
  if (impl_->persistent)
    return impl_->sync(true, std::move(callback));
  impl_->ready = true;
  callback(result::success);
  return result::success;
}
result browser_file_backend::read(std::string_view path, read_options options,
                                  file_callback<file_bytes> callback) {
  if (!callback || !options.max_bytes)
    return result::invalid_argument;
  return impl_->bytes(path, {}, options.max_bytes, std::move(callback));
}
result browser_file_backend::read_range(std::string_view path, file_range range,
                                        file_callback<file_bytes> callback) {
  if (!callback)
    return result::invalid_argument;
  return impl_->bytes(path, range, std::numeric_limits<ssize_t>::max(), std::move(callback));
}
result browser_file_backend::write(std::string_view path, file_bytes bytes, write_options options,
                                   completion_callback callback) {
  if (!callback || bytes.size() > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()) ||
      path.empty() || path.ends_with('/') ||
      (options.mode != write_mode::replace && options.mode != write_mode::create_new))
    return result::invalid_argument;
  std::string filename;
  const auto status = impl_->path(path, filename, true);
  if (status.failed())
    return status;
  descriptor file{::open(
      filename.c_str(),
      O_WRONLY | O_CREAT | (options.mode == write_mode::create_new ? O_EXCL : O_TRUNC), 0600)};
  if (file.value < 0)
    return error_result();
  if (!bytes.empty() &&
      ::write(file.value, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size()))
    return result::io;
  callback(result::success);
  return result::success;
}
result browser_file_backend::stat(std::string_view path, file_callback<file_info> callback) {
  if (!callback)
    return result::invalid_argument;
  std::string filename;
  auto status = impl_->path(path, filename);
  if (status.failed())
    return status;
  struct stat value{};
  if (::lstat(filename.c_str(), &value) != 0)
    return error_result();
  file_info output;
  status = information(value, output);
  if (status.failed())
    return status;
  callback({result::success, output});
  return result::success;
}
result browser_file_backend::list(std::string_view path, list_options options,
                                  file_callback<directory_page> callback) {
  if (!callback || options.max_entries == 0)
    return result::invalid_argument;
  std::string filename;
  auto status = impl_->path(path, filename);
  if (status.failed())
    return status;
  std::size_t skip{};
  if (!options.cursor.empty()) {
    const auto parsed =
        std::from_chars(options.cursor.data(), options.cursor.data() + options.cursor.size(), skip);
    if (parsed.ec != std::errc{} || parsed.ptr != options.cursor.data() + options.cursor.size())
      return result::invalid_argument;
  }
  std::unique_ptr<DIR, int (*)(DIR*)> directory(::opendir(filename.c_str()), ::closedir);
  if (!directory)
    return error_result();
  directory_page page;
  std::size_t index{};
  errno = 0;
  while (auto* entry = ::readdir(directory.get())) {
    const std::string_view name(entry->d_name);
    if (name == "." || name == "..")
      continue;
    if (index++ < skip)
      continue;
    if (page.entries.size() == options.max_entries) {
      page.next_cursor = std::to_string(index - 1);
      break;
    }
    struct stat value{};
    if (::lstat((filename + '/' + std::string(name)).c_str(), &value) != 0)
      return error_result();
    file_info info;
    status = information(value, info);
    if (status.failed())
      return status;
    page.entries.push_back({std::string(name), info});
    errno = 0;
  }
  if (errno)
    return error_result();
  callback({result::success, std::move(page)});
  return result::success;
}
result browser_file_backend::flush(completion_callback callback) {
  if (!callback)
    return result::invalid_argument;
  if (!emscripten_is_main_runtime_thread())
    return result::invalid_state;
  if (!impl_->ready || impl_->busy)
    return result::not_ready;
  if (!impl_->persistent)
    return result::unsupported;
  return impl_->sync(false, std::move(callback));
}
}
