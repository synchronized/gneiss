// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/platform/files/browser/fetch_file_backend.hpp"
#include "engine/platform/files/file_path.hpp"
#include "fetch_stream.hpp"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <emscripten/fetch.h>
#include <emscripten/threading.h>
#include <limits>
#include <memory>
#include <stdexcept>
namespace gneiss::engine::files {
namespace {
std::string header(emscripten_fetch_t* fetch, std::string_view name) {
  std::string headers(emscripten_fetch_get_response_headers_length(fetch) + 1, '\0');
  emscripten_fetch_get_response_headers(fetch, headers.data(), headers.size());
  std::string_view text(headers.c_str());
  while (!text.empty()) {
    const auto end = text.find('\n');
    auto line = text.substr(0, end);
    const auto colon = line.find(':');
    if (colon == name.size() &&
        std::equal(name.begin(), name.end(), line.begin(), [](char a, char b) {
          return a == (b >= 'A' && b <= 'Z' ? b + ('a' - 'A') : b);
        })) {
      line.remove_prefix(colon + 1);
      while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.remove_suffix(1);
      return std::string(line);
    }
    if (end == std::string_view::npos)
      break;
    text.remove_prefix(end + 1);
  }
  return {};
}
bool number(std::string_view text, std::uint64_t& value) {
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
struct fetch_operation : std::enable_shared_from_this<fetch_operation> {
  std::shared_ptr<fetch_operation> keep_alive;
  std::string url, range_header;
  files::callback<files::bytes> on_read;
  files::callback<files::info> on_stat;
  range range{};
  std::size_t limit{};
  bool ranged{}, head{}, finished{};
  static void error(emscripten_fetch_t* fetch) {
    auto* self = static_cast<fetch_operation*>(fetch->userData);
    if (self->finished)
      return;
    auto owner = self->keep_alive;
    self->finish(fetch, fetch->status == 404 ? result::not_found : result::io);
  }
  void finish(emscripten_fetch_t* fetch, result status, files::bytes bytes = {},
              files::info info = {}) {
    if (finished)
      return;
    finished = true;
    auto owner = std::move(keep_alive);
    // close 可能同步触发 error；finished 先置位，owner 保活至回调结束。
    emscripten_fetch_close(fetch);
    if (on_stat)
      on_stat({status, info});
    else
      on_read({status, std::move(bytes)});
  }
  bool valid_range(emscripten_fetch_t* fetch) {
    const auto text = header(fetch, "content-range");
    if (!text.starts_with("bytes "))
      return false;
    const auto dash = text.find('-', 6), slash = text.find('/', 6);
    if (dash == std::string::npos || slash == std::string::npos || dash >= slash)
      return false;
    std::uint64_t first{}, last{}, total{};
    return number(std::string_view(text).substr(6, dash - 6), first) &&
           number(std::string_view(text).substr(dash + 1, slash - dash - 1), last) &&
           number(std::string_view(text).substr(slash + 1), total) && first == range.offset &&
           last == range.offset + range.size - 1 && total > last;
  }
  static void headers_received(emscripten_fetch_t* fetch) noexcept {
    auto* self = static_cast<fetch_operation*>(fetch->userData);
    if (self->finished || self->head || fetch->readyState != 2)
      return;
    auto owner = self->keep_alive;
    try {
      if (self->ranged && fetch->status == 200) {
        self->finish(fetch, result::unsupported);
        return;
      }
      if (self->ranged && fetch->status == 206 && !self->valid_range(fetch)) {
        self->finish(fetch, result::io);
        return;
      }
      std::uint64_t length{};
      const auto text = header(fetch, "content-length");
      if (!text.empty() && (!number(text, length) || length > self->limit))
        self->finish(fetch, result::invalid_argument);
    } catch (...) {
      self->finish(fetch, result::out_of_memory);
    }
  }
  static void progress(emscripten_fetch_t* fetch) {
    auto* self = static_cast<fetch_operation*>(fetch->userData);
    if (self->finished || self->head)
      return;
    auto owner = self->keep_alive;
    if (fetch->totalBytes > self->limit || fetch->numBytes > self->limit ||
        fetch->dataOffset > self->limit - static_cast<std::size_t>(fetch->numBytes))
      self->finish(fetch, result::invalid_argument);
  }
  static void success(emscripten_fetch_t* fetch) noexcept {
    auto* self = static_cast<fetch_operation*>(fetch->userData);
    if (self->finished)
      return;
    auto owner = self->keep_alive;
    try {
      if (self->head) {
        files::info info;
        const auto length = header(fetch, "content-length");
        std::uint64_t size{};
        if (!length.empty()) {
          if (!number(length, size)) {
            self->finish(fetch, result::io);
            return;
          }
          info.size = size;
        }
        if (self->on_stat) {
          self->finish(fetch, result::success, {}, info);
          return;
        }
        // 零长度范围通过 HEAD 校验，不发送非法 Range 头。
        self->finish(fetch, !info.size                         ? result::unsupported
                            : self->range.offset <= *info.size ? result::success
                                                               : result::invalid_argument);
        return;
      }
      if (self->ranged && (fetch->status != 206 || !self->valid_range(fetch) ||
                           fetch->numBytes != self->range.size)) {
        self->finish(fetch, fetch->status == 200 ? result::unsupported : result::io);
        return;
      }
      if (fetch->numBytes > self->limit || (fetch->numBytes && !fetch->data)) {
        self->finish(fetch, result::invalid_argument);
        return;
      }
      files::bytes bytes(static_cast<std::size_t>(fetch->numBytes));
      if (!bytes.empty())
        std::memcpy(bytes.data(), fetch->data, bytes.size());
      self->finish(fetch, result::success, std::move(bytes));
    } catch (...) {
      self->finish(fetch, result::out_of_memory);
    }
  }
  result begin() {
    if (!emscripten_is_main_runtime_thread())
      return result::invalid_state;
    emscripten_fetch_attr_t attributes;
    emscripten_fetch_attr_init(&attributes);
    std::strcpy(attributes.requestMethod, head ? "HEAD" : "GET");
    attributes.attributes = head ? 0 : EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attributes.timeoutMSecs = 30000;
    attributes.userData = this;
    attributes.onsuccess = success;
    attributes.onerror = error;
    attributes.onprogress = progress;
    attributes.onreadystatechange = headers_received;
    const char* headers[]{"Range", range_header.c_str(), nullptr};
    if (ranged && !head)
      attributes.requestHeaders = headers;
    keep_alive = shared_from_this();
    if (!emscripten_fetch(&attributes, url.c_str())) {
      keep_alive.reset();
      return result::out_of_memory;
    }
    return result::success;
  }
};
}
fetch_backend::fetch_backend(std::string base_url) : base_url_(std::move(base_url)) {
  if (!base_url_.ends_with('/') ||
      (!base_url_.starts_with("https://") && !base_url_.starts_with("http://")))
    throw std::invalid_argument("HTTP 文件根地址无效");
}
result fetch_backend::open_stream(std::string_view path,
                                  files::callback<std::shared_ptr<backend_stream>> callback) {
  if (!detail::valid_file_path(path) || !callback)
    return result::invalid_argument;
  return open_fetch_stream(base_url_ + std::string(path), std::move(callback));
}
result fetch_backend::read(std::string_view path, read_options options,
                           files::callback<files::bytes> callback) {
  if (!detail::valid_file_path(path) || !callback || !options.max_bytes)
    return result::invalid_argument;
  auto request = std::make_shared<fetch_operation>();
  request->url = base_url_ + std::string(path);
  request->limit = options.max_bytes;
  request->on_read = std::move(callback);
  return request->begin();
}
result fetch_backend::read_range(std::string_view path, range range,
                                 files::callback<files::bytes> callback) {
  if (!detail::valid_file_path(path) || !callback ||
      range.offset > std::numeric_limits<std::uint64_t>::max() - range.size)
    return result::invalid_argument;
  auto request = std::make_shared<fetch_operation>();
  request->url = base_url_ + std::string(path);
  request->ranged = true;
  request->head = range.size == 0;
  request->range = range;
  request->limit = range.size;
  request->range_header = "bytes=" + std::to_string(range.offset) + "-" +
                          std::to_string(range.offset + range.size - (range.size != 0));
  request->on_read = std::move(callback);
  return request->begin();
}
result fetch_backend::stat(std::string_view path, files::callback<files::info> callback) {
  if (!detail::valid_file_path(path) || !callback)
    return result::invalid_argument;
  auto request = std::make_shared<fetch_operation>();
  request->url = base_url_ + std::string(path);
  request->head = true;
  request->on_stat = std::move(callback);
  return request->begin();
}
}
