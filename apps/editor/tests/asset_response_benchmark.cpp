// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_background_worker.hpp"
#include "asset_reimport_queue.hpp"
#include "tooling/asset_import/asset_index.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {

using clock_type = std::chrono::steady_clock;

template <typename Function> double measure(Function operation) {
  const auto start = clock_type::now();
  operation();
  return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}

void run_case(const std::filesystem::path& root, std::size_t bytes, std::size_t files) {
  namespace assets = gneiss::tooling::asset_import;
  std::filesystem::create_directories(root / "input");
  std::filesystem::create_directories(root / "assets");
  std::ifstream fixture(GNEISS_TEST_SOURCE, std::ios::binary);
  std::string source{std::istreambuf_iterator<char>(fixture), std::istreambuf_iterator<char>()};
  if (source.empty()) {
    throw std::runtime_error("fixture missing");
  }
  // 填充合法 JSON 空白隔离文件体积影响；不代表高面数模型或大纹理导入成本。
  source.resize(std::max(bytes, source.size()), ' ');
  double initial_ms = 0.0;
  std::filesystem::path imported_source;
  for (std::size_t index = 0; index < files; ++index) {
    const auto input = root / "input" / ("model-" + std::to_string(index) + ".gltf");
    std::ofstream stream(input, std::ios::binary);
    stream.write(source.data(), static_cast<std::streamsize>(source.size()));
    stream.close();
    initial_ms += measure([&] {
      const auto report = gneiss::editor::import_external_asset(root, root / "assets", input);
      if (report.result != gneiss::editor::editor_import_result::success) {
        throw std::runtime_error(report.diagnostic);
      }
      imported_source = report.source_path;
    });
  }
  double hash_ms = 0.0;
  double reimport_ms = 0.0;
  for (int iteration = 0; iteration < 3; ++iteration) {
    hash_ms += measure([&] {
      std::string hash;
      if (assets::hash_source_file(imported_source, hash).result !=
          assets::asset_index_result::success) {
        throw std::runtime_error("hash failed");
      }
    });
    reimport_ms += measure([&] {
      if (gneiss::editor::reimport_source_asset(root, root / "assets", imported_source).result !=
          gneiss::editor::editor_import_result::success) {
        throw std::runtime_error("reimport failed");
      }
    });
  }
  for (const bool changed : {false, true}) {
    if (changed) {
      std::ofstream(imported_source, std::ios::app | std::ios::binary) << ' ';
    }
    gneiss::editor::asset_reimport_queue queue({
        .debounce = std::chrono::milliseconds{0},
        .stable_read_delay = std::chrono::milliseconds{0},
        .capacity = 256U,
    });
    queue.request_rescan();
    std::size_t ticks = 0U;
    std::size_t imports = 0U;
    double maximum_tick_ms = 0.0;
    const auto rescan_ms = measure([&] {
      while (queue.is_rescanning() && ticks < 1024U) {
        maximum_tick_ms = std::max(maximum_tick_ms,
                                   measure([&] { imports += queue.tick(root, root / "assets"); }));
        ++ticks;
      }
    });
    if (queue.is_rescanning() || queue.rescan_result() != gneiss::result::success ||
        imports != (changed ? 1U : 0U)) {
      throw std::runtime_error("rescan failed");
    }
    std::printf("bytes=%zu files=%zu changed=%d initial_ms=%.3f hash_mean_ms=%.3f "
                "reimport_mean_ms=%.3f rescan_ms=%.3f max_tick_ms=%.3f ticks=%zu imports=%zu\n",
                source.size(), files, static_cast<int>(changed), initial_ms, hash_ms / 3.0,
                reimport_ms / 3.0, rescan_ms, maximum_tick_ms, ticks, imports);
  }
  gneiss::editor::asset_background_worker worker;
  worker.start(root, root / "assets");
  std::ofstream(imported_source, std::ios::app | std::ios::binary) << ' ';
  worker.request_rescan();
  worker.request_refresh();
  std::size_t samples{};
  double maximum_poll_ms{};
  bool succeeded{};
  const auto started = clock_type::now();
  while (!succeeded && clock_type::now() - started < std::chrono::seconds(60)) {
    maximum_poll_ms = std::max(
        maximum_poll_ms, measure([&] {
          const auto status = worker.status();
          if (!status.error.empty()) {
            throw std::runtime_error(status.error);
          }
          std::vector<gneiss::editor::asset_reimport_event> events;
          (void)worker.poll_events(events);
          for (const auto& event : events) {
            succeeded = succeeded || event.state == gneiss::editor::asset_reimport_state::succeeded;
            if (event.state == gneiss::editor::asset_reimport_state::failed) {
              throw std::runtime_error(event.import.diagnostic);
            }
          }
          gneiss::editor::asset_browser_model browser;
          gneiss::editor::asset_browser_result result{};
          (void)worker.poll_browser(browser, result);
        }));
    ++samples;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  worker.request_stop();
  if (!succeeded) {
    throw std::runtime_error("background import timed out");
  }
  std::printf("background bytes=%zu files=%zu wall_ms=%.3f max_poll_ms=%.3f samples=%zu\n",
              source.size(), files,
              std::chrono::duration<double, std::milli>(clock_type::now() - started).count(),
              maximum_poll_ms, samples);
}

} // namespace

int main(int argc, char** argv) try {
  if (argc != 2) {
    return 1;
  }
  const std::filesystem::path root = argv[1];
  if (std::filesystem::exists(root)) {
    std::fputs("Output must be a new directory\n", stderr);
    return 2;
  }
  constexpr std::size_t mib = 1024U * 1024U;
  run_case(root / "one-mib", mib, 1U);
  run_case(root / "sixteen-mib", 16U * mib, 1U);
  run_case(root / "sixty-four-mib", 64U * mib, 1U);
  run_case(root / "many-files", mib, 64U);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 3;
}
