// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_background_worker.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace gneiss::editor {

struct asset_background_worker::implementation {
  enum class command_kind : std::uint8_t { notify, import, external, cancel };
  struct command {
    command_kind kind;
    std::filesystem::path path;
  };
  static constexpr std::size_t capacity = 256U;
  mutable std::mutex mutex;
  std::condition_variable wake;
  std::deque<command> commands;
  std::vector<asset_reimport_event> events;
  std::unique_ptr<asset_browser_model> browser;
  asset_browser_result browser_result{asset_browser_result::success};
  asset_worker_status state;
  bool refresh{};
  bool rescan{};
  std::uint64_t revision{};
  std::uint64_t cancellation{};
  import_function execute;
  std::filesystem::path project;
  std::filesystem::path assets;
  // 最后声明，确保析构 join 后才销毁线程访问的其他字段。
  std::jthread thread;

  void run() {
    try {
      work();
    } catch (const std::exception& error) {
      std::scoped_lock lock(mutex);
      state.error = error.what();
    } catch (...) {
      std::scoped_lock lock(mutex);
      state.error = "资产工作线程异常";
    }
    std::scoped_lock lock(mutex);
    state.active = false;
    state.stopped = true;
    state.stage = "Stopped";
  }

  void work() {
    std::uint64_t task_revision{};
    std::uint64_t task_cancellation{};
    std::deque<command> manual;
    const auto importer = [&](const auto& project_path, const auto& asset_path, const auto& source,
                              bool external) {
      {
        std::scoped_lock lock(mutex);
        state.stage = "Importing";
        state.source = source;
      }
      tooling::asset_import::import_control control{
          .cancelled =
              [&] {
                std::scoped_lock lock(mutex);
                return state.stopping || revision != task_revision;
              },
          .begin_commit =
              [&] {
                std::scoped_lock lock(mutex);
                if (state.stopping || revision != task_revision) {
                  return false;
                }
                state.stage = "Committing";
                return true;
              }};
      editor_import_report report;
      try {
        report = execute(project_path, asset_path, source, external, control);
      } catch (const std::exception& error) {
        report.result = editor_import_result::import_failed;
        report.source_path = source;
        report.diagnostic = error.what();
      } catch (...) {
        report.result = editor_import_result::import_failed;
        report.source_path = source;
        report.diagnostic = "导入器发生未知异常";
      }
      {
        std::scoped_lock lock(mutex);
        using tooling::asset_import::import_asset_result;
        if (!state.stopping && cancellation == task_cancellation &&
            (report.import.result == import_asset_result::cancelled ||
             report.import.result == import_asset_result::source_changed)) {
          // 外部源已复制时只重试工程内副本，避免重复创建带后缀的源文件。
          const auto retry_source = report.source_path.empty() ? source : report.source_path;
          if (manual.size() < capacity) {
            manual.push_back({report.source_path.empty() && external ? command_kind::external
                                                                     : command_kind::import,
                              retry_source});
          } else {
            ++state.dropped;
            rescan = true;
          }
        }
      }
      return report;
    };
    const auto make_queue = [&] {
      return std::make_unique<asset_reimport_queue>(
          asset_reimport_queue_options{},
          [&](const auto& p, const auto& a, const auto& s) { return importer(p, a, s, false); });
    };
    auto queue = make_queue();
    std::size_t observed_drops{};
    bool browser_dirty{};
    for (;;) {
      std::deque<command> incoming;
      bool scan{};
      bool refresh_browser{};
      {
        std::unique_lock lock(mutex);
        state.active = false;
        state.pending = manual.size() + queue->pending_count();
        state.rescanning = queue->is_rescanning() || rescan;
        state.rescan_result = queue->rescan_result();
        // 结果队列背压，成功通知不会因 UI 暂停消费而被静默丢弃。
        wake.wait_for(lock, std::chrono::milliseconds(20), [&] {
          return state.stopping || (!state.paused && events.size() < capacity &&
                                    (!commands.empty() || rescan || refresh));
        });
        if (state.stopping) {
          break;
        }
        if (state.paused || events.size() >= capacity) {
          continue;
        }
        incoming.swap(commands);
        scan = std::exchange(rescan, false);
        refresh_browser = std::exchange(refresh, false);
        task_revision = revision;
        task_cancellation = cancellation;
        state.active = true;
        state.stage = "Checking sources";
        state.source.clear();
      }
      std::vector<asset_reimport_event> completed;
      for (auto& item : incoming) {
        switch (item.kind) {
        case command_kind::cancel:
          manual.clear();
          queue = make_queue();
          observed_drops = 0U;
          scan = false;
          break;
        case command_kind::notify:
          (void)queue->notify(item.path);
          break;
        default:
          if (manual.size() < capacity) {
            manual.push_back(std::move(item));
          } else {
            std::scoped_lock lock(mutex);
            ++state.dropped;
            editor_import_report rejected;
            rejected.source_path = item.path;
            rejected.result = editor_import_result::import_failed;
            rejected.diagnostic = "导入队列已满，请重试";
            completed.push_back({.state = asset_reimport_state::failed,
                                 .relative_path = item.path,
                                 .import = std::move(rejected)});
          }
          break;
        }
      }
      if (scan) {
        queue->request_rescan();
      }
      if (!manual.empty()) {
        auto item = std::move(manual.front());
        manual.pop_front();
        auto report = importer(project, assets, item.path, item.kind == command_kind::external);
        const auto succeeded = report.result == editor_import_result::success;
        completed.push_back(
            {.state = succeeded ? asset_reimport_state::succeeded : asset_reimport_state::failed,
             .relative_path = item.path,
             .import = std::move(report)});
        browser_dirty = true;
      } else {
        (void)queue->tick(project, assets);
        (void)queue->poll_events(completed);
        for (const auto& event : completed) {
          browser_dirty = browser_dirty || event.state == asset_reimport_state::succeeded ||
                          event.state == asset_reimport_state::removed;
        }
      }
      if (refresh_browser || (browser_dirty && manual.empty() && queue->pending_count() == 0U &&
                              !queue->is_rescanning())) {
        browser_dirty = false;
        bool stop{};
        {
          std::scoped_lock lock(mutex);
          stop = state.stopping;
          state.stage = "Refreshing browser";
        }
        if (!stop) {
          auto next = std::make_unique<asset_browser_model>();
          const auto result = next->refresh(project, assets);
          std::scoped_lock lock(mutex);
          browser = std::move(next);
          browser_result = result;
        }
      }
      {
        std::scoped_lock lock(mutex);
        for (auto& event : completed) {
          events.push_back(std::move(event));
        }
        state.dropped += queue->dropped_candidate_count() - observed_drops;
        observed_drops = queue->dropped_candidate_count();
        state.stage = "Idle";
      }
    }
  }
};

asset_background_worker::asset_background_worker(import_function importer)
    : impl_(std::make_unique<implementation>()) {
  impl_->execute =
      importer ? std::move(importer)
               : import_function{[](const auto& project, const auto& assets, const auto& source,
                                    bool external, const auto& control) {
                   return external
                              ? import_external_asset_controlled(project, assets, source, control)
                              : reimport_source_asset_controlled(project, assets, source, control);
                 }};
}
asset_background_worker::~asset_background_worker() { request_stop(); }

void asset_background_worker::start(std::filesystem::path project, std::filesystem::path assets) {
  std::scoped_lock lock(impl_->mutex);
  if (impl_->thread.joinable()) {
    return;
  }
  impl_->project = std::move(project);
  impl_->assets = std::move(assets);
  impl_->refresh = true;
  impl_->thread = std::jthread([this] { impl_->run(); });
}

result asset_background_worker::notify(const std::filesystem::path& relative) {
  if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
      relative != relative.lexically_normal()) {
    return result::invalid_argument;
  }
  std::scoped_lock lock(impl_->mutex);
  if (impl_->state.stopping || impl_->state.stopped) {
    return result::not_ready;
  }
  ++impl_->revision;
  for (const auto& command : impl_->commands) {
    if (command.kind == implementation::command_kind::notify && command.path == relative) {
      return result::success;
    }
  }
  if (impl_->commands.size() >= implementation::capacity) {
    ++impl_->state.dropped;
    impl_->rescan = true;
    return result::not_ready;
  }
  impl_->commands.push_back({implementation::command_kind::notify, relative});
  impl_->wake.notify_one();
  return result::success;
}

bool asset_background_worker::import_asset(const std::filesystem::path& source, bool external) {
  std::scoped_lock lock(impl_->mutex);
  if (impl_->state.stopping || impl_->state.stopped ||
      impl_->commands.size() >= implementation::capacity) {
    return false;
  }
  impl_->commands.push_back(
      {external ? implementation::command_kind::external : implementation::command_kind::import,
       source});
  impl_->wake.notify_one();
  return true;
}
void asset_background_worker::request_rescan() {
  std::scoped_lock lock(impl_->mutex);
  impl_->rescan = true;
  impl_->wake.notify_one();
}
void asset_background_worker::request_refresh() {
  std::scoped_lock lock(impl_->mutex);
  impl_->refresh = true;
  impl_->wake.notify_one();
}
void asset_background_worker::cancel() {
  std::scoped_lock lock(impl_->mutex);
  ++impl_->revision;
  ++impl_->cancellation;
  impl_->commands.clear();
  impl_->commands.push_back({implementation::command_kind::cancel, {}});
  impl_->rescan = false;
  impl_->refresh = false;
  impl_->wake.notify_one();
}
void asset_background_worker::request_stop() {
  std::scoped_lock lock(impl_->mutex);
  impl_->state.stopping = true;
  if (!impl_->thread.joinable()) {
    impl_->state.stopped = true;
  }
  impl_->wake.notify_one();
}
void asset_background_worker::set_paused(bool paused) {
  std::scoped_lock lock(impl_->mutex);
  impl_->state.paused = paused;
  impl_->wake.notify_one();
}
asset_worker_status asset_background_worker::status() const {
  std::scoped_lock lock(impl_->mutex);
  auto state = impl_->state;
  state.pending += impl_->commands.size();
  return state;
}
bool asset_background_worker::is_rescanning() const { return status().rescanning; }
result asset_background_worker::rescan_result() const { return status().rescan_result; }
std::size_t asset_background_worker::dropped_candidate_count() const { return status().dropped; }
std::size_t asset_background_worker::poll_events(std::vector<asset_reimport_event>& events) {
  std::scoped_lock lock(impl_->mutex);
  events.swap(impl_->events);
  impl_->events.clear();
  impl_->wake.notify_one();
  return events.size();
}
bool asset_background_worker::poll_browser(asset_browser_model& browser,
                                           asset_browser_result& result) {
  std::unique_ptr<asset_browser_model> next;
  {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->browser) {
      return false;
    }
    next = std::move(impl_->browser);
    result = impl_->browser_result;
  }
  const auto selection = std::string{browser.selection()};
  browser = std::move(*next);
  (void)browser.select(selection);
  return true;
}

} // namespace gneiss::editor
