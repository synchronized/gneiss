// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_background_worker.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "tooling/asset_import/asset_index.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <iterator>
#include <thread>

namespace {
using namespace std::chrono_literals;
using namespace gneiss::editor;
namespace ai = gneiss::tooling::asset_import;
std::string read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input}, {}};
}
template <class Predicate> bool wait_until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return true;
}
int verify(const std::filesystem::path& root) {
  const auto assets = root / "assets";
  const auto source = root / "sources" / "triangle.gltf";
  std::filesystem::create_directories(assets);
  std::filesystem::create_directories(source.parent_path());
  std::filesystem::copy_file(GNEISS_TEST_SOURCE, source);
  const auto baseline = reimport_source_asset(root, assets, source);
  if (baseline.result != editor_import_result::success) {
    return 1;
  }
  const auto index = root / ".gneiss" / "asset-index.json";
  const auto old_index = read(index);
  const auto marker = baseline.import.output_directory / "old-output-marker";
  std::ofstream(marker) << "old";
  auto intact = [&] { return read(index) == old_index && read(marker) == "old"; };
  // 真正解析及暂存后拒绝提交，旧索引和目录均不能改变。
  const auto cancelled = reimport_source_asset_controlled(
      root, assets, source, {.cancelled = {}, .begin_commit = [] { return false; }});
  if (cancelled.import.result != ai::import_asset_result::cancelled || !intact()) {
    return 2;
  }
  unsigned checks{};
  const auto stale = reimport_source_asset_controlled(root, assets, source,
                                                      {.cancelled =
                                                           [&] {
                                                             if (++checks == 3U) {
                                                               std::ofstream(source, std::ios::app)
                                                                   << " ";
                                                             }
                                                             return false;
                                                           },
                                                       .begin_commit = {}});
  if (stale.import.result != ai::import_asset_result::source_changed || !intact()) {
    return 3;
  }
  // 阻止索引暂存文件写入，验证已替换的派生目录被回滚。
  const auto obstruction = std::filesystem::path(index.string() + ".gneiss-staging");
  const auto failed = reimport_source_asset_controlled(
      root, assets, source, {.cancelled = {}, .begin_commit = [&] {
                               std::filesystem::create_directory(obstruction);
                               std::ofstream(obstruction / "occupied") << "x";
                               return true;
                             }});
  if (failed.import.result != ai::import_asset_result::index_update_failed || !intact()) {
    return 4;
  }
  std::filesystem::remove_all(obstruction);

  std::promise<void> release;
  auto gate = release.get_future().share();
  std::atomic_bool entered{};
  std::atomic_uint calls{};
  asset_background_worker worker(
      [&](const auto& p, const auto& a, const auto& s, bool, const ai::import_control& control) {
        if (calls.fetch_add(1U) == 0U) {
          entered = true;
          gate.wait();
        }
        return reimport_source_asset_controlled(p, a, s, control);
      });
  worker.start(root, assets);
  if (!worker.import_asset(source)) {
    release.set_value();
    return 5;
  }
  if (!wait_until([&] { return entered.load(); })) {
    release.set_value();
    return 6;
  }
  // 工作线程被可控屏障阻塞，主线程仍能轮询及投递；超时只用来发现死锁。
  auto ui = std::async(std::launch::async, [&] {
    for (unsigned i = 0; i < 1000U; ++i) {
      (void)worker.status();
      worker.request_refresh();
    }
    worker.cancel();
    worker.set_paused(true);
  });
  const auto responsive = ui.wait_for(2s) == std::future_status::ready;
  release.set_value();
  ui.wait();
  if (!responsive || !wait_until([&] { return !worker.status().active; })) {
    return 7;
  }
  std::vector<asset_reimport_event> events;
  (void)worker.poll_events(events);
  if (events.size() != 1U ||
      events.front().import.import.result != ai::import_asset_result::cancelled || !intact()) {
    return 8;
  }
  worker.set_paused(false);
  // 取消之后可重新提交；成功结果只能由主线程消费。
  if (!worker.import_asset(source)) {
    return 9;
  }
  if (!wait_until([&] {
        (void)worker.poll_events(events);
        for (const auto& event : events) {
          if (event.state == asset_reimport_state::succeeded) {
            return true;
          }
        }
        return false;
      })) {
    return 10;
  }
  if (std::filesystem::exists(marker)) {
    return 11;
  }
  asset_browser_model browser;
  asset_browser_result browser_result{};
  if (!wait_until([&] { return worker.poll_browser(browser, browser_result); }) ||
      browser_result != asset_browser_result::success || browser.entries().empty()) {
    return 12;
  }
  worker.request_stop();
  if (!wait_until([&] { return worker.status().stopped; }) || worker.import_asset(source)) {
    return 13;
  }

  // 新通知使当前任务失效并重排，最终索引哈希必须对应最新源文件。
  std::promise<void> release_stale;
  auto stale_gate = release_stale.get_future().share();
  std::atomic_bool stale_entered{};
  std::atomic_uint stale_calls{};
  asset_background_worker superseded(
      [&](const auto& p, const auto& a, const auto& s, bool, const ai::import_control& control) {
        if (stale_calls.fetch_add(1U) == 0U) {
          stale_entered = true;
          stale_gate.wait();
        }
        return reimport_source_asset_controlled(p, a, s, control);
      });
  superseded.start(root, assets);
  (void)superseded.import_asset(source);
  if (!wait_until([&] { return stale_entered.load(); })) {
    release_stale.set_value();
    return 14;
  }
  std::ofstream(source, std::ios::app) << "  ";
  (void)superseded.notify("triangle.gltf");
  release_stale.set_value();
  if (!wait_until([&] {
        (void)superseded.poll_events(events);
        for (const auto& event : events) {
          if (event.state == asset_reimport_state::succeeded) {
            return true;
          }
        }
        return false;
      })) {
    return 15;
  }
  superseded.request_stop();
  if (!wait_until([&] { return superseded.status().stopped; })) {
    return 16;
  }
  ai::asset_index loaded;
  std::string hash;
  if (ai::load_asset_index(index, loaded).result != ai::asset_index_result::success ||
      ai::hash_source_file(source, hash).result != ai::asset_index_result::success ||
      loaded.entries.front().content_hash != hash) {
    return 17;
  }
  // 暂停必须在工作线程空闲后阻止后续任务，队列有界且拒绝可观察。
  asset_background_worker paused;
  paused.set_paused(true);
  paused.start(root, assets);
  for (unsigned i = 0; i < 256U; ++i) {
    if (!paused.import_asset(source)) {
      return 18;
    }
  }
  if (paused.import_asset(source) || paused.status().active) {
    return 19;
  }
  paused.cancel();
  paused.request_stop();
  if (!wait_until([&] { return paused.status().stopped; })) {
    return 20;
  }

  // 协作宿主显式驱动；服务持锁提交、轮询和取消均不得隐式运行任务。
  gneiss::tasks::task_scheduler cooperative({.mode = gneiss::tasks::execution_mode::cooperative});
  unsigned cooperative_calls{};
  asset_background_worker cooperative_worker(
      [&](const auto& p, const auto& a, const auto& s, bool, const ai::import_control& control) {
        ++cooperative_calls;
        return reimport_source_asset_controlled(p, a, s, control);
      },
      &cooperative);
  cooperative_worker.start(root, assets);
  cooperative_worker.set_paused(true);
  (void)cooperative_worker.import_asset(source);
  (void)cooperative.run_ready();
  if (cooperative_calls != 0U) {
    return 23;
  }
  cooperative_worker.set_paused(false);
  if (cooperative_calls != 0U) {
    return 24;
  }
  if (!wait_until([&] {
        (void)cooperative.run_ready();
        (void)cooperative_worker.poll_events(events);
        for (const auto& event : events) {
          if (event.state == asset_reimport_state::succeeded) {
            return true;
          }
        }
        return false;
      }) ||
      cooperative_calls != 1U) {
    return 25;
  }
  cooperative_worker.request_stop();
  if (!cooperative_worker.status().stopped) {
    return 26;
  }

  // 停止后不得提交或接收新工作，析构必须等待不可抢占调用实际返回。
  std::promise<void> release_close;
  const auto close_gate = release_close.get_future().share();
  std::atomic_bool close_entered{};
  auto closing = std::make_unique<asset_background_worker>(
      [&](const auto& p, const auto& a, const auto& s, bool, const ai::import_control& control) {
        close_entered = true;
        close_gate.wait();
        return reimport_source_asset_controlled(p, a, s, control);
      });
  closing->start(root, assets);
  (void)closing->import_asset(source);
  if (!wait_until([&] { return close_entered.load(); })) {
    release_close.set_value();
    return 21;
  }
  const auto before_close = read(index);
  closing->request_stop();
  const auto rejected = !closing->import_asset(source);
  auto joined =
      std::async(std::launch::async, [owned = std::move(closing)]() mutable { owned.reset(); });
  const auto waiting = joined.wait_for(30ms) == std::future_status::timeout;
  release_close.set_value();
  if (!rejected || !waiting || joined.wait_for(5s) != std::future_status::ready ||
      read(index) != before_close) {
    return 22;
  }
  return 0;
}
} // namespace

int main() { // NOLINT(bugprone-exception-escape)
  const auto root = std::filesystem::temp_directory_path() / "gneiss-background-assets-test";
  std::filesystem::remove_all(root);
  const auto result = verify(root);
  std::printf("background assets test: %d\n", result);
  std::filesystem::remove_all(root);
  return result;
}
