// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "imgui_adapter.hpp"
#if defined(GNEISS_TEST_BACKGROUND_ASSETS)
#include "asset_background_worker.hpp"
#include "editor_camera.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#endif

#include <gneiss/engine/application.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

struct state final {
  gneiss::editor::imgui_adapter ui;
#if defined(GNEISS_TEST_BACKGROUND_ASSETS)
  gneiss::editor::asset_background_worker* assets{};
  gneiss::editor::editor_camera camera;
#endif
  HWND window = nullptr;
  std::uint32_t frames = 0U;
  bool check_pointer = false;
  std::uint32_t pointer_check_frame = 0U;
};

BOOL CALLBACK find_window(HWND window, LPARAM data) {
  DWORD process = 0U;
  (void)GetWindowThreadProcessId(window, &process);
  if (process == GetCurrentProcessId() && GetWindow(window, GW_OWNER) == nullptr) {
    *reinterpret_cast<HWND*>(data) = window;
    return FALSE;
  }
  return TRUE;
}

gneiss_result update(gneiss_application application, const gneiss_frame_time* time, void* data) {
  auto& value = *static_cast<state*>(data);
#if defined(GNEISS_TEST_BACKGROUND_ASSETS)
  if (!value.assets->status().active ||
      value.camera.update({.delta_seconds = 0.016F, .dolly = 0.01F}) != gneiss::result::success) {
    return GNEISS_ERROR_INVALID_STATE;
  }
#endif
  RECT client{};
  if (GetClientRect(value.window, &client) == 0) {
    return GNEISS_ERROR_INTERNAL;
  }
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  auto operation = gneiss_application_get_window_size(application, &width, &height);
  if (operation != GNEISS_SUCCESS || width != static_cast<std::uint32_t>(client.right) ||
      height != static_cast<std::uint32_t>(client.bottom)) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  operation = value.ui.begin_frame(application, *time);
  if (operation != GNEISS_SUCCESS) {
    return operation;
  }
  const auto& io = ImGui::GetIO();
  gneiss_pointer_state pointer = GNEISS_POINTER_STATE_INIT;
  const auto pointer_result = gneiss_application_get_pointer_state(application, &pointer);
  const auto aligned =
      io.DisplaySize.x == static_cast<float>(std::max(width, 1U)) &&
      io.DisplaySize.y == static_cast<float>(std::max(height, 1U)) &&
      io.DisplayFramebufferScale.x == 1.0F && io.DisplayFramebufferScale.y == 1.0F &&
      (!value.check_pointer || value.frames < value.pointer_check_frame ||
       (pointer_result == GNEISS_SUCCESS && pointer.x == 123.0F && pointer.y == 87.0F &&
        (pointer.is_inside == 0U ? !ImGui::IsMousePosValid()
                                 : (io.MousePos.x == pointer.x && io.MousePos.y == pointer.y))));
  if (!aligned) {
    std::printf("alignment failure: display=%.1f,%.1f client=%u,%u pointer=%.1f,%.1f\n",
                static_cast<double>(io.DisplaySize.x), static_cast<double>(io.DisplaySize.y), width,
                height, static_cast<double>(io.MousePos.x), static_cast<double>(io.MousePos.y));
  }
  ImGui::SetNextWindowPos(ImVec2(20.0F, 20.0F));
  ImGui::Begin("Native interaction probe");
  ImGui::Text("Frame %u: %u x %u", value.frames, width, height);
  ImGui::End();
  operation = value.ui.submit(application);
  ++value.frames;
  return aligned ? operation : GNEISS_ERROR_INVALID_STATE;
}

} // namespace

int main() {
#if defined(GNEISS_TEST_BACKGROUND_ASSETS)
  std::promise<void> release;
  auto gate = release.get_future().share();
  std::atomic_bool entered{};
  gneiss::editor::asset_background_worker worker([&](const auto&, const auto&, const auto&, bool,
                                                     const auto& control) {
    entered = true;
    gate.wait();
    gneiss::editor::editor_import_report report;
    report.import.result =
        control.cancelled() ? gneiss::tooling::asset_import::import_asset_result::cancelled
                            : gneiss::tooling::asset_import::import_asset_result::invalid_argument;
    return report;
  });
  struct release_guard {
    std::promise<void>& value;
    ~release_guard() { value.set_value(); }
  } guard{release};
  worker.start(std::filesystem::temp_directory_path(),
               std::filesystem::temp_directory_path() / "unused-gneiss-assets");
  (void)worker.import_asset("blocked-test-input");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!entered && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!entered) {
    return 4;
  }
#endif
  state value;
#if defined(GNEISS_TEST_BACKGROUND_ASSETS)
  value.assets = &worker;
#endif
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_width = 640U;
  desc.window_height = 480U;
  desc.window_title = "Gneiss 0.37 interaction probe";
  desc.window_title_length = 27U;
  desc.user_data = &value;
  desc.update = update;
  gneiss::application application;
  if (gneiss::application::create_native(desc, application) != gneiss::result::success) {
    return 1;
  }
  (void)EnumWindows(find_window, reinterpret_cast<LPARAM>(&value.window));
  if (value.window == nullptr || value.ui.initialize(application.get()) != GNEISS_SUCCESS) {
    return 2;
  }
#if defined(GNEISS_TEST_BACKGROUND_ASSETS)
  gneiss_world world{};
  if (application.get_world(world) != gneiss::result::success ||
      value.camera.initialize(world) != gneiss::result::success) {
    return 5;
  }
#endif
  // 使用窗口自己的 DPI 上下文查询物理客户区，避免测试进程受到 DPI 虚拟化影响。
  const auto previous = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(value.window));
  const auto run = [&] { return application.run(3U) == gneiss::result::success; };
  bool success = run();
  for (int index = 0; index < 18 && success; ++index) {
    success = SetWindowPos(value.window, nullptr, -3000, -3000, 640 + (index * 19),
                           480 + ((index % 5) * 31), SWP_NOACTIVATE | SWP_NOZORDER) != FALSE &&
              run();
  }
  for (int index = 0; index < 3 && success; ++index) {
    (void)ShowWindow(value.window, SW_MINIMIZE);
    success = run();
    (void)ShowWindow(value.window, SW_SHOWNOACTIVATE);
    success = success && run();
  }
  // 合成 WM_DPICHANGED 验证消息路径；不声称覆盖物理显示器迁移。
  for (const WORD dpi : {WORD{96}, WORD{120}, WORD{144}, WORD{192}, WORD{96}}) {
    RECT target{.left = -3000, .top = -3000, .right = -2040, .bottom = -2280};
    (void)SendMessageW(value.window, WM_DPICHANGED, MAKEWPARAM(dpi, dpi),
                       reinterpret_cast<LPARAM>(&target));
    (void)SendMessageW(value.window, WM_MOUSEMOVE, 0U, MAKELPARAM(123, 87));
    TRACKMOUSEEVENT tracking{.cbSize = sizeof(TRACKMOUSEEVENT),
                             .dwFlags = TME_CANCEL | TME_LEAVE,
                             .hwndTrack = value.window,
                             .dwHoverTime = 0U};
    (void)TrackMouseEvent(&tracking);
    value.check_pointer = true;
    value.pointer_check_frame = value.frames + 2U;
    success = success && run();
    std::printf("synthetic DPI=%u, frames=%u, aligned=%d\n", static_cast<unsigned>(dpi),
                value.frames, static_cast<int>(success));
  }
#if defined(GNEISS_TEST_BACKGROUND_ASSETS)
  value.camera.shutdown();
  worker.request_stop();
#endif
  value.ui.shutdown(application.get());
  if (previous != nullptr) {
    (void)SetThreadDpiAwarenessContext(previous);
  }
  std::printf("18 resizes, 3 minimize/restore cycles, 5 synthetic DPI changes: %s\n",
              success ? "passed" : "failed");
  return success ? 0 : 3;
}
