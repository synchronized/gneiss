// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_asset_service.hpp"

#include <algorithm>
#include <deque>
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
  struct command {
    command_kind kind{};
    std::string uri{};
    std::shared_ptr<const std::string> content{};
    std::uint64_t hash{};
  };
  struct batch {
    std::vector<author_asset_change> changes;
    result operation{result::success};
    bool scanning{};
    std::uint64_t revision{};
  };
  struct backend {
    author_asset_monitor monitor;
    bool initialized{};
    bool baseline{true};
  };

  tasks::task_scheduler& scheduler;
  tasks::task_scope scope;
  std::shared_ptr<backend> worker{std::make_shared<backend>()};
  std::shared_ptr<batch> active;
  std::filesystem::path root;
  std::deque<command> commands;
  std::deque<author_asset_change> ready;
  std::unordered_map<std::string, author_asset_change> prepared;
  author_asset_change current;
  std::uint64_t revision{};
  bool rescan{};
  bool scanning{};
  bool stopping{};
  result operation{result::success};

  explicit implementation(tasks::task_scheduler& value)
      : scheduler(value), scope(value.make_scope()) {}

  bool enqueue(command value) {
    if (stopping) {
      return false;
    }
    if (value.kind == command_kind::probe &&
        std::ranges::any_of(commands, [&value](const auto& item) {
          return item.kind == command_kind::probe && item.uri == value.uri;
        })) {
      return true;
    }
    if (commands.size() >= 256U) {
      rescan = true;
      return false;
    }
    commands.push_back(std::move(value));
    return true;
  }

  void pump() {
    std::vector<tasks::task_completion> completions;
    scheduler.poll(scope, completions, 1U);
    if (!completions.empty() && active) {
      operation = completions.front().outcome.state == tasks::task_state::succeeded
                      ? active->operation
                      : result::internal;
      scanning = active->scanning;
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
    if (stopping || root.empty() || active || !ready.empty() || !prepared.empty() ||
        (commands.empty() && !rescan && !scanning)) {
      return;
    }
    auto output = std::make_shared<batch>();
    output->revision = revision;
    std::vector<command> work;
    const auto count = std::min<std::size_t>(8U, commands.size());
    work.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
      work.push_back(commands[index]);
    }
    const auto scan = rescan;
    tasks::task_handle handle;
    const auto accepted = scheduler.submit(
        {.name = "author-assets.check", .scope = scope},
        [state = worker, output, work = std::move(work), scan,
         asset_root = root](const tasks::task_context& context) {
          if (!state->initialized) {
            output->operation = state->monitor.initialize(asset_root, false);
            if (output->operation != result::success) {
              return tasks::task_outcome{};
            }
            state->initialized = true;
          }
          for (const auto& item : work) {
            if (context.stop_requested()) {
              return tasks::task_outcome{tasks::task_state::cancelled};
            }
            if (item.kind == command_kind::saved) {
              output->changes.clear();
              output->operation = state->monitor.acknowledge_content(item.uri, *item.content);
            } else if (item.kind == command_kind::applied) {
              state->monitor.accept_fingerprint(item.uri, item.hash);
            } else {
              auto change = state->monitor.observe(path_for(item.uri), false);
              if (change.state != author_asset_change_state::idle) {
                output->changes.push_back(std::move(change));
              }
            }
          }
          if (scan) {
            state->monitor.request_rescan();
          }
          std::vector<std::filesystem::path> paths;
          output->operation = state->monitor.poll_rescan(paths, 8U);
          for (const auto& path : paths) {
            if (context.stop_requested()) {
              return tasks::task_outcome{tasks::task_state::cancelled};
            }
            if (state->baseline) {
              state->monitor.establish_baseline(uri_for(path));
            } else {
              auto change = state->monitor.observe(path, false);
              if (change.state != author_asset_change_state::idle) {
                output->changes.push_back(std::move(change));
              }
            }
          }
          output->scanning = state->monitor.is_rescanning();
          if (!output->scanning) {
            state->baseline = false;
          }
          return tasks::task_outcome{};
        },
        handle);
    if (accepted == tasks::submit_result::success) {
      active = std::move(output);
      for (std::size_t index = 0U; index < count; ++index) {
        commands.pop_front();
      }
      rescan = false;
    } else if (accepted != tasks::submit_result::full) {
      operation = result::invalid_state;
    }
  }
};

author_asset_service::author_asset_service(tasks::task_scheduler& scheduler)
    : impl_(std::make_unique<implementation>(scheduler)) {}
author_asset_service::~author_asset_service() {
  request_stop();
  (void)impl_->scheduler.close_scope(impl_->scope);
}
result author_asset_service::initialize(const std::filesystem::path& root) {
  if (root.empty() || !impl_->root.empty() || impl_->stopping) {
    return result::invalid_argument;
  }
  impl_->root = root;
  impl_->rescan = true;
  impl_->pump();
  return result::success;
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
    impl_->current.operation = result::invalid_state;
    impl_->current.message = "外部结构资产已变化；当前场景含未保存修改，未自动覆盖";
  }
  return impl_->current;
}
result author_asset_service::acknowledge(std::string_view uri, std::string content) {
  if (!uri.starts_with("asset://")) {
    return result::invalid_argument;
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
    return result::invalid_state;
  }
  impl_->rescan = true;
  return result::success;
}
void author_asset_service::request_rescan() { impl_->rescan = true; }
result author_asset_service::poll_rescan(std::vector<std::filesystem::path>& output,
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
         !impl_->commands.empty() || !impl_->ready.empty() || !impl_->prepared.empty();
}
result author_asset_service::rescan_result() const { return impl_->operation; }
void author_asset_service::mark_applied(std::string_view uri) {
  if (impl_->current.uri != uri || impl_->current.operation != result::success) {
    return;
  }
  if (!impl_->enqueue({.kind = implementation::command_kind::applied,
                       .uri = std::string(uri),
                       .hash = impl_->current.fingerprint})) {
    mark_failed(uri, result::invalid_state);
    return;
  }
  impl_->current.state = author_asset_change_state::applied;
  impl_->current.message = "外部结构资产已应用";
}
void author_asset_service::mark_failed(std::string_view uri, result operation) {
  impl_->current = {.state = author_asset_change_state::failed,
                    .uri = std::string(uri),
                    .operation = operation,
                    .message = "外部结构资产应用失败"};
}
const author_asset_change& author_asset_service::status() const { return impl_->current; }
void author_asset_service::request_stop() {
  impl_->stopping = true;
  impl_->scheduler.cancel_scope(impl_->scope);
}
bool author_asset_service::stopped() const {
  return impl_->stopping && impl_->scheduler.idle(impl_->scope);
}
} // namespace gneiss::editor
