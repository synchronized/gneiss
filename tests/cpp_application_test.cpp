// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/application.hpp>

#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
struct context {
  std::uint64_t frame_count{}, shutdown_count{}, initialize_count{}, polls{}, clock{};
  std::atomic<std::uint64_t> logs;
};
gneiss::result update(gneiss::application_ref application, const gneiss::frame_time& time,
                      void* user_data) noexcept {
  auto& state = *static_cast<context*>(user_data);
  ++state.frame_count;
  if (time.frame_index == 0U) {
    return application.set_paused(true);
  }
  return time.delta_ns == 0U && time.is_paused ? gneiss::result::success : gneiss::result::internal;
}
}
namespace {
int verify_failed_initialization() {
  using gneiss::result;
  gneiss::application application;
  struct failure_context {
    int diagnostics{}, shutdowns{};
  } failure;
  gneiss::application_desc failed;
  failed.callbacks.user_data = &failure;
  failed.callbacks.initialize = [](void*) noexcept { return result::dependency_failed; };
  failed.callbacks.shutdown = [](void* data) noexcept {
    ++static_cast<failure_context*>(data)->shutdowns;
  };
  failed.callbacks.diagnostic = [](gneiss::application_ref app, const gneiss::diagnostic& event,
                                   void* data) noexcept {
    if (!app.is_valid() && event.operation == result::dependency_failed &&
        event.severity == gneiss::diagnostic_severity::error && !event.module.empty()) {
      ++static_cast<failure_context*>(data)->diagnostics;
    }
  };
  if (gneiss::application::create(failed, application) != result::dependency_failed ||
      application.is_valid() || failure.diagnostics != 1 || failure.shutdowns != 1) {
    return 12;
  }
  return 0;
}
}
int main() {
  using gneiss::result;
  static_assert(!std::is_convertible_v<gneiss::released_application, gneiss_application>);
  static_assert(!std::is_copy_constructible_v<gneiss::released_application>);
  context state;
  gneiss::application application;
  {
    gneiss::application_desc desc;
    desc.callbacks.user_data = &state;
    desc.callbacks.update = update;
    desc.callbacks.initialize = [](void* data) noexcept {
      ++static_cast<context*>(data)->initialize_count;
      return result::success;
    };
    desc.callbacks.shutdown = [](void* data) noexcept {
      ++static_cast<context*>(data)->shutdown_count;
    };
    desc.callbacks.poll_events = [](void* data, bool& close) noexcept {
      ++static_cast<context*>(data)->polls;
      close = false;
      return result::success;
    };
    desc.callbacks.now_ns = [](void* data) noexcept {
      return ++static_cast<context*>(data)->clock;
    };
    desc.callbacks.log = [](gneiss::application_ref, const gneiss::log_event& event,
                            void* data) noexcept {
      if (event.category == "cpp" && event.message == "moved" &&
          event.operation == result::success) {
        ++static_cast<context*>(data)->logs;
      }
    };
    if (gneiss::application::create(desc, application).failed()) {
      return 1;
    }
  } // 描述已离开作用域，回调表不能借用它。
  std::uint32_t width{};
  std::uint32_t height{};
  if (application.get_window_size(width, height).failed() || width != 1280 || height != 720 ||
      application.run(2).failed() || state.frame_count != 2) {
    return 2;
  }
  auto moved = std::move(application);
  if (moved.run(1).failed() || state.frame_count != 3) {
    return 3;
  }
  result closed;
  std::thread wrong_thread([&] { closed = moved.reset(); });
  wrong_thread.join();
  if (closed != result::invalid_state || !moved.is_valid()) {
    return 4;
  }
  auto released = moved.release();
  if (moved.is_valid() || released.get() == 0 ||
      gneiss_application_run(released.get(), 1) != GNEISS_SUCCESS) {
    return 5;
  }
  if (gneiss::application::adopt(std::move(released), application).failed() ||
      // NOLINTNEXTLINE(bugprone-use-after-move): adopt 成功后载体必须可查询为空。
      released.get() != 0 || application.log({.category = "cpp", .message = "moved"}).failed()) {
    return 6;
  }
  const auto original = application.get();
  gneiss::application_desc invalid;
  invalid.platform = static_cast<gneiss::application_platform>(42);
  if (gneiss::application::create(invalid, application) != result::invalid_argument ||
      application.get() != original) {
    return 7;
  }
  invalid.platform = gneiss::application_platform::callback;
  invalid.window_flags = static_cast<gneiss::application_window_flags>(8);
  if (gneiss::application::create(invalid, application) != result::invalid_argument) {
    return 8;
  }
  if (application.reset().failed() || state.initialize_count != 1 || state.shutdown_count != 1 ||
      state.logs != 1) {
    return 9;
  }
  {
    gneiss::application_desc desc;
    desc.callbacks.user_data = &state;
    desc.callbacks.shutdown = [](void* data) noexcept {
      ++static_cast<context*>(data)->shutdown_count;
    };
    if (gneiss::application::create(desc, application).failed()) {
      return 10;
    }
    auto token = application.release();
  } // 未 adopt 的载体必须关闭 Application。
  if (state.shutdown_count != 2) {
    return 11;
  }
  return verify_failed_initialization();
}
