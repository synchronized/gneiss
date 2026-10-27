// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/platform/files/memory_file_backend.hpp"

#include "engine/platform/files/file_path.hpp"

#include <iterator>
#include <utility>

namespace gneiss::engine::files {
namespace {
std::string directory_prefix(std::string_view path) {
  std::string prefix(path);
  if (!prefix.empty() && !prefix.ends_with('/'))
    prefix.push_back('/');
  return prefix;
}
}
result memory_backend::read(std::string_view path, read_options options,
                            files::callback<files::bytes> callback) {
  if (!callback || !detail::valid_file_path(path) || options.max_bytes == 0)
    return result::invalid_argument;
  const auto found = files_.find(path);
  if (found == files_.end()) {
    const auto prefix = directory_prefix(path);
    const auto child = files_.lower_bound(prefix);
    return path.empty() || (child != files_.end() && child->first.starts_with(prefix))
               ? result::invalid_argument
               : result::not_found;
  }
  if (found->second.size() > options.max_bytes)
    return result::invalid_argument;
  callback({result::success, found->second});
  return result::success;
}
result memory_backend::read_range(std::string_view path, range range,
                                  files::callback<files::bytes> callback) {
  if (!callback || !detail::valid_file_path(path))
    return result::invalid_argument;
  const auto found = files_.find(path);
  if (found == files_.end()) {
    const auto prefix = directory_prefix(path);
    const auto child = files_.lower_bound(prefix);
    return path.empty() || (child != files_.end() && child->first.starts_with(prefix))
               ? result::invalid_argument
               : result::not_found;
  }
  const auto& bytes = found->second;
  if (range.offset > bytes.size() || range.size > bytes.size() - range.offset)
    return result::invalid_argument;
  const auto first = bytes.begin() + static_cast<std::ptrdiff_t>(range.offset);
  callback({result::success, files::bytes(first, first + static_cast<std::ptrdiff_t>(range.size))});
  return result::success;
}
result memory_backend::write(std::string_view path, files::bytes bytes, write_options options,
                             completion_callback callback) {
  if (!callback || path.empty() || path.ends_with('/') || !detail::valid_file_path(path) ||
      (options.mode != write_mode::replace && options.mode != write_mode::create_new))
    return result::invalid_argument;
  if (options.mode == write_mode::create_new && files_.contains(path))
    return result::invalid_state;
  const auto prefix = directory_prefix(path);
  const auto child = files_.lower_bound(prefix);
  if (child != files_.end() && child->first.starts_with(prefix))
    return result::invalid_state;
  for (auto slash = path.find('/'); slash != std::string_view::npos;
       slash = path.find('/', slash + 1)) {
    if (files_.contains(path.substr(0, slash)))
      return result::invalid_state;
  }
  files_.insert_or_assign(std::string(path), std::move(bytes));
  callback(result::success);
  return result::success;
}
result memory_backend::stat(std::string_view path, files::callback<files::info> callback) {
  if (!callback || !detail::valid_file_path(path))
    return result::invalid_argument;
  if (const auto found = files_.find(path); found != files_.end()) {
    callback({result::success, {kind::regular, found->second.size()}});
    return result::success;
  }
  const auto prefix = directory_prefix(path);
  const auto found = files_.lower_bound(prefix);
  if (!path.empty() && (found == files_.end() || !found->first.starts_with(prefix)))
    return result::not_found;
  callback({result::success, {kind::directory, std::nullopt}});
  return result::success;
}
result memory_backend::list(std::string_view path, list_options options,
                            files::callback<directory_page> callback) {
  if (!callback || !detail::valid_file_path(path) || options.max_entries == 0 ||
      options.max_entries > 1024)
    return result::invalid_argument;
  if (files_.contains(path))
    return result::invalid_argument;
  const auto prefix = directory_prefix(path);
  auto entry = files_.lower_bound(prefix);
  if (!path.empty() && (entry == files_.end() || !entry->first.starts_with(prefix)))
    return result::not_found;
  // 路径排序不等于直接子项排序（例如 a-x 与 a/child），只保留最小的一页及探测项。
  std::map<std::string, files::info, std::less<>> candidates;
  for (; entry != files_.end() && entry->first.starts_with(prefix); ++entry) {
    const std::string_view relative = std::string_view(entry->first).substr(prefix.size());
    const auto slash = relative.find('/');
    const auto name = relative.substr(0, slash);
    if (name <= options.cursor)
      continue;
    files::info info{kind::directory, std::nullopt};
    if (slash == std::string_view::npos)
      info = {kind::regular, entry->second.size()};
    candidates.try_emplace(std::string(name), info);
    if (candidates.size() > options.max_entries + 1)
      candidates.erase(std::prev(candidates.end()));
  }
  directory_page page;
  for (auto& [name, info] : candidates) {
    if (page.entries.size() == options.max_entries) {
      page.next_cursor = page.entries.back().name;
      break;
    }
    page.entries.push_back({name, info});
  }
  callback({result::success, std::move(page)});
  return result::success;
}
} // namespace gneiss::engine::files
