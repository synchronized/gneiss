// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_asset_service.hpp"
#include "engine/core/result.hpp"

#include <algorithm>
#include <deque>
#include <unordered_set>
#include <utility>

namespace gneiss::editor {
namespace {
std::string uri_for(const std::filesystem::path& path) {
  const auto value = path.lexically_normal().generic_u8string();
  return "asset://" + std::string(reinterpret_cast<const char*>(value.data()), value.size());
}
std::filesystem::path path_for(std::string_view uri) {
  const auto text = uri.substr(8U);
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
bool structural(std::string_view uri) {
  return uri.ends_with(".scene.json") || uri.ends_with(".prefab.json");
}
} // namespace

struct author_asset_service::implementation {
  enum class command_kind : std::uint8_t { probe, saved, applied };
  struct read_receipt {
    gneiss::engine::file_completion<gneiss::engine::file_bytes> value;
    bool ready{};
    std::uint64_t revision{};
  };
  struct command {
    command_kind kind{};
    std::string uri{};
    std::shared_ptr<const std::string> content{};
    std::uint64_t hash{};
    bool baseline{};
    std::shared_ptr<read_receipt> read{};
  };
  struct batch {
    std::vector<author_asset_change> changes;
    gneiss::engine::result operation{gneiss::engine::result::success};
    std::uint64_t revision{};
  };
  struct backend {
    author_asset_monitor monitor;
  };

  struct list_receipt {
    gneiss::engine::file_completion<gneiss::engine::directory_page> value;
    bool ready{};
  };
  struct directory {
    std::string uri;
    std::string cursor;
  };
  std::deque<directory> directories;
  std::deque<std::string> known;
  std::unordered_set<std::string> seen;
  std::shared_ptr<list_receipt> listing;
  gneiss::engine::file_request_id list_request;
  bool baseline{true};
  std::shared_ptr<gneiss::engine::file_service> files;
  gneiss::engine::file_request_id read_request;
  std::shared_ptr<read_receipt> reading;
  gneiss::engine::task_scheduler& scheduler;
  gneiss::engine::task_scope scope;
  std::shared_ptr<backend> worker{std::make_shared<backend>()};
  std::shared_ptr<batch> active;
  std::deque<command> commands;
  std::deque<author_asset_change> ready;
  std::unordered_map<std::string, author_asset_change> prepared;
  author_asset_change current;
  std::uint64_t revision{};
  bool rescan{};
  bool scanning{};
  bool stopping{};
  gneiss::engine::result operation{gneiss::engine::result::success};

  explicit implementation(gneiss::engine::task_scheduler& value)
      : scheduler(value), scope(value.make_scope()) {}

  bool enqueue(command value) {
    if (stopping) {
      return false;
    }
    if (value.kind == command_kind::probe &&
        std::ranges::any_of(commands, [&value](const auto& item) {
          return item.kind == command_kind::probe && item.uri == value.uri;
        })) {
      // 同一路径读取在途时收到新事件，完成后补扫，不能吞掉较新的写入。
      rescan = true;
      return true;
    }
    if (commands.size() >= 256U) {
      rescan = true;
      return false;
    }
    commands.push_back(std::move(value));
    return true;
  }

  // 仅在 CPU 任务结束且待探测队列为空时推进；每次最多一页。
  void scan() {
    if (!scanning) {
      if (!rescan)
        return;
      rescan = false;
      scanning = true;
      operation = gneiss::engine::result::success;
      directories = {{"asset://", {}}};
      seen.clear();
      const auto uris = worker->monitor.known_uris();
      known.assign(uris.begin(), uris.end());
    }
    for (unsigned i = 0; i < 8 && !known.empty(); ++i) {
      auto uri = std::move(known.front());
      known.pop_front();
      if (seen.insert(uri).second)
        (void)enqueue({.kind = command_kind::probe, .uri = std::move(uri), .baseline = baseline});
    }
    if (!commands.empty())
      return;
    if (listing) {
      if (!listing->ready)
        return;
      auto value = std::move(listing->value);
      listing.reset();
      list_request = {};
      if (value.status.failed()) {
        operation = value.status;
        directories.clear();
        known.clear();
        scanning = false;
        return;
      }
      auto& dir = directories.front();
      const auto prefix = dir.uri;
      dir.cursor = std::move(value.data.next_cursor);
      if (dir.cursor.empty())
        directories.pop_front();
      for (const auto& entry : value.data.entries) {
        const auto uri = prefix + entry.name;
        if (entry.info.kind == gneiss::engine::file_kind::directory)
          directories.push_back({uri + "/", {}});
        else if (structural(uri) && seen.insert(uri).second)
          (void)enqueue({.kind = command_kind::probe, .uri = uri, .baseline = baseline});
      }
      return;
    }
    if (directories.empty()) {
      scanning = false;
      baseline = false;
      return;
    }
    auto receipt = std::make_shared<list_receipt>();
    const auto status = files->list(
        directories.front().uri, {8, directories.front().cursor},
        [receipt](auto value) {
          receipt->value = std::move(value);
          receipt->ready = true;
        },
        list_request);
    if (status == gneiss::engine::result::not_ready)
      return;
    if (status.failed()) {
      receipt->value.status = status;
      receipt->ready = true;
    }
    listing = std::move(receipt);
  }

  void pump() {
    if (reading && reading->ready) {
      reading.reset();
      read_request = {};
    }
    std::vector<gneiss::engine::task_completion> completions;
    scheduler.poll(scope, completions, 1U);
    if (!completions.empty() && active) {
      operation = completions.front().outcome.state == gneiss::engine::task_state::succeeded
                      ? active->operation
                      : gneiss::engine::result::internal;
      if (active->revision == revision) {
        for (auto& change : active->changes) {
          ready.push_back(std::move(change));
        }
      } else {
        // 保存期间的旧结果不可清除新基线；补扫保证其他路径也不会漏掉。
        rescan = true;
      }
      active.reset();
    }
    if (stopping || !files || active || !ready.empty() || !prepared.empty() ||
        (commands.empty() && !rescan && !scanning)) {
      return;
    }
    if (commands.empty()) {
      scan();
      if (commands.empty())
        return;
    }
    if (!commands.empty() && commands.front().kind == command_kind::probe) {
      auto& item = commands.front();
      if (item.read && item.read->ready && item.read->revision != revision)
        item.read.reset();
      if (!item.read) {
        if (reading)
          return;
        auto receipt = std::make_shared<read_receipt>();
        receipt->revision = revision;
        const auto accepted = files->read(
            item.uri, {16U * 1024U * 1024U},
            [receipt](auto value) {
              receipt->value = std::move(value);
              receipt->ready = true;
            },
            read_request);
        if (accepted == gneiss::engine::result::not_ready)
          return;
        item.read = receipt;
        if (accepted.failed()) {
          receipt->value.status = accepted;
          receipt->ready = true;
        } else {
          reading = receipt;
        }
      }
      if (!item.read->ready)
        return;
    }
    auto output = std::make_shared<batch>();
    output->revision = revision;
    std::vector<command> work;
    const auto count = std::min<std::size_t>(1U, commands.size());
    work.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
      work.push_back(commands[index]);
    }
    gneiss::engine::task_handle handle;
    const auto accepted = scheduler.submit(
        {.name = "author-assets.check", .scope = scope},
        [state = worker, output,
         work = std::move(work)](const gneiss::engine::task_context& context) {
          for (const auto& item : work) {
            if (context.stop_requested()) {
              return gneiss::engine::task_outcome{gneiss::engine::task_state::cancelled};
            }
            if (item.kind == command_kind::saved) {
              output->changes.clear();
              output->operation = state->monitor.acknowledge_content(item.uri, *item.content);
            } else if (item.kind == command_kind::applied) {
              state->monitor.accept_fingerprint(item.uri, item.hash);
            } else {
              std::uint64_t hash = 14695981039346656037ULL;
              for (const auto byte : item.read->value.data) {
                hash ^= std::to_integer<unsigned char>(byte);
                hash *= 1099511628211ULL;
              }
              if (item.baseline) {
                if (item.read->value.status.ok())
                  state->monitor.establish_baseline(item.uri, hash);
              } else {
                auto change = state->monitor.observe(path_for(item.uri), false,
                                                     item.read->value.status, hash);
                if (change.state != author_asset_change_state::idle)
                  output->changes.push_back(std::move(change));
              }
            }
          }
          return gneiss::engine::task_outcome{};
        },
        handle);
    if (accepted == gneiss::engine::submit_result::success) {
      active = std::move(output);
      for (std::size_t index = 0U; index < count; ++index) {
        commands.pop_front();
      }
    } else if (accepted != gneiss::engine::submit_result::full) {
      operation = gneiss::engine::result::invalid_state;
    }
  }
};

author_asset_service::author_asset_service(gneiss::engine::task_scheduler& scheduler)
    : impl_(std::make_unique<implementation>(scheduler)) {}
author_asset_service::~author_asset_service() {
  request_stop();
  (void)impl_->scheduler.close_scope(impl_->scope);
}
gneiss::engine::result
author_asset_service::initialize(std::shared_ptr<gneiss::engine::file_service> files) {
  if (!files || impl_->files || impl_->stopping) {
    return gneiss::engine::result::invalid_argument;
  }
  impl_->files = std::move(files);
  impl_->rescan = true;
  impl_->pump();
  return gneiss::engine::result::success;
}
author_asset_change author_asset_service::observe(const std::filesystem::path& path, bool dirty) {
  const auto uri = uri_for(path);
  if (!structural(uri) || path.is_absolute() ||
      std::ranges::find(path, std::filesystem::path("..")) != path.end()) {
    return {};
  }
  const auto found = impl_->prepared.find(uri);
  if (found == impl_->prepared.end()) {
    (void)impl_->enqueue({.kind = implementation::command_kind::probe, .uri = uri});
    return {};
  }
  impl_->current = std::move(found->second);
  impl_->prepared.erase(found);
  if (dirty) {
    impl_->current.state = author_asset_change_state::conflict;
    impl_->current.operation = gneiss::engine::result::invalid_state;
    impl_->current.message = "外部结构资产已变化；当前场景含未保存修改，未自动覆盖";
  }
  return impl_->current;
}
gneiss::engine::result author_asset_service::acknowledge(std::string_view uri,
                                                         std::string content) {
  if (!uri.starts_with("asset://")) {
    return gneiss::engine::result::invalid_argument;
  }
  std::erase_if(impl_->commands, [uri](const auto& item) { return item.uri == uri; });
  if (impl_->commands.size() >= 256U) {
    const auto probe = std::ranges::find_if(impl_->commands, [](const auto& item) {
      return item.kind == implementation::command_kind::probe;
    });
    if (probe != impl_->commands.end()) {
      impl_->commands.erase(probe);
    }
  }
  ++impl_->revision;
  impl_->ready.clear();
  impl_->prepared.clear();
  impl_->current = {};
  if (!impl_->enqueue({.kind = implementation::command_kind::saved,
                       .uri = std::string(uri),
                       .content = std::make_shared<const std::string>(std::move(content))})) {
    return gneiss::engine::result::invalid_state;
  }
  impl_->rescan = true;
  return gneiss::engine::result::success;
}
void author_asset_service::request_rescan() { impl_->rescan = true; }
gneiss::engine::result author_asset_service::poll_rescan(std::vector<std::filesystem::path>& output,
                                                         std::size_t budget) {
  impl_->pump();
  while (budget-- != 0U && !impl_->ready.empty()) {
    auto change = std::move(impl_->ready.front());
    impl_->ready.pop_front();
    const auto uri = change.uri;
    if (!impl_->prepared.contains(uri)) {
      output.push_back(path_for(uri));
    }
    impl_->prepared.insert_or_assign(uri, std::move(change));
  }
  return impl_->operation;
}
bool author_asset_service::is_rescanning() const {
  return impl_->rescan || impl_->scanning || static_cast<bool>(impl_->active) ||
         static_cast<bool>(impl_->reading) || !impl_->commands.empty() || !impl_->ready.empty() ||
         !impl_->prepared.empty();
}
gneiss::engine::result author_asset_service::rescan_result() const { return impl_->operation; }
void author_asset_service::mark_applied(std::string_view uri) {
  if (impl_->current.uri != uri || impl_->current.operation != gneiss::engine::result::success) {
    return;
  }
  if (!impl_->enqueue({.kind = implementation::command_kind::applied,
                       .uri = std::string(uri),
                       .hash = impl_->current.fingerprint})) {
    mark_failed(uri, gneiss::engine::result::invalid_state);
    return;
  }
  impl_->current.state = author_asset_change_state::applied;
  impl_->current.message = "外部结构资产已应用";
}
void author_asset_service::mark_failed(std::string_view uri, gneiss::engine::result operation) {
  impl_->current = {.state = author_asset_change_state::failed,
                    .uri = std::string(uri),
                    .operation = operation,
                    .message = "外部结构资产应用失败"};
}
const author_asset_change& author_asset_service::status() const { return impl_->current; }
void author_asset_service::request_stop() {
  impl_->stopping = true;
  if (impl_->read_request.value)
    (void)impl_->files->cancel(impl_->read_request);
  if (impl_->list_request.value)
    (void)impl_->files->cancel(impl_->list_request);
  impl_->commands.clear();
  impl_->scheduler.cancel_scope(impl_->scope);
}
bool author_asset_service::stopped() const {
  return impl_->stopping && (!impl_->reading || impl_->reading->ready) &&
         (!impl_->listing || impl_->listing->ready) && impl_->scheduler.idle(impl_->scope);
}
} // namespace gneiss::editor
