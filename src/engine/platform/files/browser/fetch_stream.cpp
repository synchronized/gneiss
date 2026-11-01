// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "fetch_stream.hpp"
#include <emscripten/threading.h>
#include <limits>
#include <memory>
#include <utility>
extern "C" {
void gneiss_fetch_stream_open(const char*, void*, void (*)(void*, int, int));
void gneiss_fetch_stream_read(int, void*, std::size_t, void*,
                              void (*)(void*, int, std::size_t, int));
void gneiss_fetch_stream_close(int);
}
namespace gneiss::engine::files {
namespace {
error translate(int status) {
  switch (status) {
  case 0:
    return error::success;
  case 2:
    return error::not_found;
  case 3:
    return error::unsupported;
  case 4:
    return error::cancelled;
  case 5:
    return error::out_of_memory;
  default:
    return error::io;
  }
}
class fetch_stream final : public backend_stream,
                           public std::enable_shared_from_this<fetch_stream> {
public:
  int handle{};
  std::uint64_t offset{};
  bool busy{}, ended{};
  struct reading {
    std::shared_ptr<fetch_stream> owner;
    files::bytes bytes;
    files::callback<block> callback;
  };
  ~fetch_stream() override { close(); }
  void close() noexcept override {
    ended = true;
    if (const auto previous = std::exchange(handle, 0))
      gneiss_fetch_stream_close(previous);
  }
  static void received(void* context, int status, std::size_t count, int eof) {
    std::unique_ptr<reading> pending(static_cast<reading*>(context));
    auto& self = *pending->owner;
    self.busy = false;
    auto result = translate(status);
    if (result.ok() && (count > pending->bytes.size() || (!count && !eof) ||
                        count > std::numeric_limits<std::uint64_t>::max() - self.offset))
      result = error::io;
    if (result.failed()) {
      self.close();
      pending->callback({result, {}});
      return;
    }
    pending->bytes.resize(count);
    const auto offset = self.offset;
    self.offset += count;
    if (eof)
      self.close();
    pending->callback({error::success, {std::move(pending->bytes), offset, eof != 0}});
  }
  error read_next(std::size_t maximum_bytes, files::callback<block> callback) override {
    if (!emscripten_is_main_runtime_thread())
      return error::invalid_state;
    if (!callback || !maximum_bytes)
      return error::invalid_argument;
    if (ended || !handle)
      return error::invalid_state;
    if (busy)
      return error::not_ready;
    auto pending = std::make_unique<reading>(
        reading{shared_from_this(), files::bytes(maximum_bytes), std::move(callback)});
    auto* data = pending->bytes.data();
    busy = true;
    gneiss_fetch_stream_read(handle, data, maximum_bytes, pending.release(), received);
    return error::success;
  }
};
struct opening {
  files::callback<std::shared_ptr<backend_stream>> callback;
  std::shared_ptr<fetch_stream> stream = std::make_shared<fetch_stream>();
  static void opened(void* context, int status, int handle) {
    std::unique_ptr<opening> pending(static_cast<opening*>(context));
    pending->stream->handle = handle;
    const auto result = translate(status);
    if (result.failed())
      pending->callback({result, {}});
    else
      pending->callback({result, std::move(pending->stream)});
  }
};
}
error open_fetch_stream(std::string url,
                        files::callback<std::shared_ptr<backend_stream>> callback) {
  if (!emscripten_is_main_runtime_thread())
    return error::invalid_state;
  if (!callback)
    return error::invalid_argument;
  auto pending = std::make_unique<opening>();
  pending->callback = std::move(callback);
  gneiss_fetch_stream_open(url.c_str(), pending.release(), opening::opened);
  return error::success;
}
}
