// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/value_result.hpp"
#include "function/application/main_loop_wakeup.hpp"
#include "function/render/detail/executor.hpp"
#include "function/render/detail/resource_identity.hpp"
#include <gneiss/engine/core/base/error.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace {
// 执行中的输入继续计费；预算拒绝不能丢弃旧帧，替换回收后才归还额度。
bool frame_input_budget() {
  using namespace gneiss;
  using namespace gneiss::render::detail;
  std::mutex mutex;
  std::condition_variable ready;
  bool started = false;
  bool release = false;
  std::vector<unsigned> drawn;
  threaded_render_executor executor;
  if (executor
          .initialize(
              [&](auto& packet, auto&) {
                if (packet.surface.width == 1) {
                  std::unique_lock lock(mutex);
                  started = true;
                  ready.notify_all();
                  ready.wait(lock, [&] { return release; });
                }
                drawn.push_back(packet.surface.width);
                return error::success;
              },
              {.pending_frames = 1, .frame_input_bytes = 12})
          .failed())
    return false;
  frame_packet first;
  first.surface.width = 1;
  auto running = executor.submit_frame(std::move(first), render::frame_policy::replaceable, {}, 8);
  if (!running)
    return false;
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return started; });
  }
  frame_packet second;
  second.surface.width = 2;
  auto waiting = executor.submit_frame(std::move(second), render::frame_policy::replaceable, {}, 3);
  auto full = executor.submit_frame({}, render::frame_policy::required, {}, 2);
  auto large = executor.submit_frame({}, render::frame_policy::replaceable, {}, 13);
  const bool preserved = !executor.try_take_frame_completion();
  frame_packet latest;
  latest.surface.width = 4;
  auto replacement =
      executor.submit_frame(std::move(latest), render::frame_policy::replaceable, {}, 1);
  auto dropped = executor.try_take_frame_completion();
  auto busy = executor.query_stats();
  {
    std::lock_guard lock(mutex);
    release = true;
  }
  ready.notify_all();
  const auto flushed = executor.flush();
  auto empty = executor.query_stats();
  executor.stop();
  return waiting && !full && full.error() == error::not_ready && !large &&
         large.error() == error::out_of_memory && preserved && replacement && dropped &&
         dropped->sequence == *waiting && dropped->dropped && busy.frame_input_bytes == 9 &&
         busy.frame_input_high_watermark == 12 && flushed.ok() && empty.frame_input_bytes == 0 &&
         drawn == std::vector<unsigned>{1, 4};
}

// 被替换帧的捕获析构可查询执行器；丢帧完成必须晚于捕获释放。
bool replaced_frame_releases_outside_lock() {
  using namespace gneiss;
  using namespace gneiss::render::detail;
  std::mutex mutex;
  std::condition_variable ready;
  bool started = false;
  bool release = false;
  bool released = false;
  bool premature = false;
  threaded_render_executor executor;
  if (executor.initialize([](auto&, auto&) { return error::success; }, {.pending_frames = 1})
          .failed())
    return false;
  auto blocker = executor.submit_command([&](const auto&) {
    std::unique_lock lock(mutex);
    started = true;
    ready.notify_all();
    ready.wait(lock, [&] { return release; });
    return error::success;
  });
  if (!blocker)
    return false;
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return started; });
  }
  auto payload = std::shared_ptr<int>(new int{}, [&](int* value) {
    (void)executor.query_stats();
    premature = executor.try_take_frame_completion().has_value();
    delete value;
    released = true;
  });
  auto old = executor.submit_frame(
      {}, render::frame_policy::replaceable,
      [payload = std::move(payload)]() -> result<frame_packet> { return frame_packet{}; });
  auto replacement = executor.submit_frame({});
  auto dropped = executor.try_take_frame_completion();
  {
    std::lock_guard lock(mutex);
    release = true;
  }
  ready.notify_all();
  const auto flushed = executor.flush();
  executor.stop();
  return old && replacement && released && !premature && dropped && dropped->sequence == *old &&
         dropped->dropped && flushed.ok();
}

// 同一共享所有者只计费一次；容量拒绝不执行，零数据销毁命令仍可进入。
bool command_payload_budget() {
  using namespace gneiss;
  using namespace gneiss::render::detail;
  std::mutex mutex;
  std::condition_variable ready;
  bool started = false;
  bool release = false;
  threaded_render_executor executor;
  if (executor
          .initialize([](auto&, auto&) { return error::success; },
                      {.commands = 8, .command_payload_bytes = 12})
          .failed())
    return false;
  auto data = std::make_shared<int>();
  auto blocker = executor.submit_command(
      [&, data](const auto&) {
        std::unique_lock lock(mutex);
        started = true;
        ready.notify_all();
        ready.wait(lock, [&] { return release; });
        return error::success;
      },
      render_command_policy::once, 8, data);
  if (!blocker)
    return false;
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return started; });
  }
  unsigned calls{};
  auto work = [data, &calls](const auto&) {
    ++calls;
    return error::success;
  };
  auto shared = executor.submit_command(work, render_command_policy::once, 8, data);
  auto inconsistent = executor.submit_command(work, render_command_policy::once, 7, data);
  auto owned = executor.submit_command(work, render_command_policy::once, 4);
  auto full = executor.submit_command(work, render_command_policy::once, 1);
  auto oversized = executor.submit_command(work, render_command_policy::once, 13);
  auto retirement = executor.submit_command(work);
  const auto busy = executor.query_stats();
  {
    std::lock_guard lock(mutex);
    release = true;
  }
  ready.notify_all();
  const auto drained = executor.flush();
  const auto empty = executor.query_stats();
  auto retry = executor.submit_command(work, render_command_policy::once, 12);
  const auto retried = executor.flush();
  executor.stop();
  return shared && !inconsistent && inconsistent.error() == error::invalid_argument && owned &&
         !full && full.error() == error::not_ready && !oversized &&
         oversized.error() == error::out_of_memory && retirement &&
         busy.command_payload_bytes == 12 && busy.command_payload_high_watermark == 12 &&
         drained.ok() && empty.command_payload_bytes == 0 && retry && retried.ok() && calls == 4;
}

// 捕获资源的析构也是命令完成的一部分；未释放前不得归还容量或发布完成。
bool command_retirement_precedes_completion(gneiss::error terminal) {
  using namespace gneiss;
  using namespace gneiss::render::detail;
  std::mutex mutex;
  std::condition_variable ready;
  bool retiring = false;
  bool release = false;
  threaded_render_executor executor;
  if (executor.initialize([](auto&, auto&) { return error::success; }, {.commands = 1}).failed())
    return false;
  auto payload = std::shared_ptr<int>(new int{}, [&](int* value) {
    std::unique_lock lock(mutex);
    retiring = true;
    ready.notify_all();
    ready.wait(lock, [&] { return release; });
    delete value;
  });
  const auto submitted = executor.submit_command(
      [payload = std::move(payload), terminal](const auto&) { return terminal; });
  if (!submitted)
    return false;
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return retiring; });
  }
  const auto premature = executor.try_take_command_completion();
  const auto saturated = executor.submit_command([](const auto&) { return error::success; });
  {
    std::lock_guard lock(mutex);
    release = true;
  }
  ready.notify_all();
  const auto flushed = executor.flush();
  const auto completed = executor.try_take_command_completion();
  const auto retried = executor.submit_command([](const auto&) { return error::success; });
  const auto retry_flushed = executor.flush();
  const auto retry_completed = executor.try_take_command_completion();
  const bool drained = !executor.try_take_command_completion();
  executor.stop();
  return !premature && !saturated && saturated.error() == error::not_ready && flushed.ok() &&
         completed && completed->sequence == *submitted && completed->status == terminal &&
         retried && retry_flushed.ok() && retry_completed &&
         retry_completed->sequence == *retried && retry_completed->status.ok() && drained;
}

// 帧可以替换，夹在帧之间的状态命令必须按序且仅执行一次。
bool ordered_commands_survive_frame_replacement() {
  using namespace gneiss;
  using namespace gneiss::render::detail;
  std::mutex mutex;
  std::condition_variable ready;
  bool started = false;
  bool release = false;
  int scene_state = 0;
  int discarded_prepares = 0;
  bool captured_in_order = false;
  const auto producer = std::this_thread::get_id();
  auto identity = std::make_shared<gneiss::render::detail::resource_identity<mesh_data>>();
  auto old_version = std::make_shared<mesh_data>();
  auto future_version = std::make_shared<mesh_data>();
  identity->publish(old_version);
  std::vector<int> observed;
  threaded_render_executor executor;
  if (executor
          .initialize(
              [&](auto&, auto&) {
                observed.push_back(scene_state);
                return error::success;
              },
              {.pending_frames = 1})
          .failed())
    return false;
  auto gate = executor.submit_command([&](const auto&) {
    std::unique_lock lock(mutex);
    started = true;
    ready.notify_all();
    ready.wait(lock, [&] { return release; });
    scene_state = 1;
    return error::success;
  });
  if (!gate)
    return false;
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return started; });
  }
  auto discarded_image = std::make_shared<gneiss::render::frame_image>();
  std::weak_ptr<gneiss::render::frame_image> discarded_lifetime = discarded_image;
  gneiss::render::detail::frame_packet discarded;
  discarded.readback = std::move(discarded_image);
  auto old_frame = executor.submit_frame(
      {}, gneiss::render::frame_policy::replaceable,
      [packet = std::move(discarded),
       &discarded_prepares]() mutable -> result<gneiss::render::detail::frame_packet> {
        ++discarded_prepares;
        return std::move(packet);
      });
  auto update = executor.submit_command([&](const auto&) {
    scene_state = 7;
    return error::success;
  });
  auto drawn_image = std::make_shared<gneiss::render::frame_image>();
  std::weak_ptr<gneiss::render::frame_image> drawn_lifetime = drawn_image;
  gneiss::render::detail::frame_packet drawn;
  drawn.readback = std::move(drawn_image);
  auto new_frame = executor.submit_frame(
      {}, gneiss::render::frame_policy::replaceable,
      [&, packet = std::move(drawn)]() mutable -> result<gneiss::render::detail::frame_packet> {
        captured_in_order = scene_state == 7 && std::this_thread::get_id() != producer &&
                            identity->read_consumed() == old_version &&
                            identity->read() == future_version;
        return std::move(packet);
      });
  const bool discarded_released = discarded_lifetime.expired();
  auto destroy = executor.submit_command([&](const auto&) {
    scene_state = 0;
    identity->consume(future_version);
    return error::success;
  });
  identity->accept(future_version); // 已接受的未来版本不能泄漏给前一帧。
  {
    std::lock_guard lock(mutex);
    release = true;
  }
  ready.notify_all();
  const auto flushed = executor.flush();
  // 不领取完成回执，帧数据仍应已经释放。
  const bool drawn_released = drawn_lifetime.expired();
  auto dropped = executor.try_take_frame_completion();
  auto completed = executor.try_take_frame_completion();
  const auto stats = executor.query_stats();
  executor.stop();
  return old_frame && update && new_frame && destroy && flushed.ok() && discarded_released &&
         drawn_released && captured_in_order && discarded_prepares == 0 &&
         identity->read_consumed() == future_version && observed == std::vector<int>{7} &&
         scene_state == 0 && dropped && dropped->sequence == *old_frame && dropped->dropped &&
         completed && completed->sequence == *new_frame && completed->status.ok() &&
         stats.executed_commands == 3 && stats.replaced_frames == 1;
}

bool resumable_command_allows_frames(gneiss::error terminal) {
  using namespace gneiss;
  using namespace gneiss::render::detail;
  std::atomic_bool frame_executed{};
  auto started = std::make_shared<gneiss::app::main_loop_wakeup>();
  auto lifetime = std::make_shared<int>(7);
  std::weak_ptr<int> retained = lifetime;
  threaded_render_executor executor;
  if (executor.initialize(
          [&](auto&, auto&) {
            frame_executed.store(true);
            return gneiss::error::success;
          },
          {.pending_frames = 1, .commands = 1, .command_payload_bytes = 8}) !=
      gneiss::error::success) {
    return false;
  }
  const auto observed = started->snapshot();
  std::uint64_t command{};
  if (gneiss::test_support::assign_value(
          executor.submit_command(
              [&, lifetime](const auto& reporter) {
                reporter.report(render_command_stage::uploading, 0U, 1U);
                started->notify();
                return frame_executed.load() ? terminal : gneiss::error::not_ready;
              },
              render_command_policy::resumable, 8, lifetime),
          command) != gneiss::error::success) {
    return false;
  }
  lifetime.reset();
  const bool began = started->wait(observed, std::chrono::seconds(2));
  std::optional<render_command_completion> premature;
  const bool no_completion = !(premature = executor.try_take_command_completion());
  const bool alive = !retained.expired() && executor.query_stats().command_payload_bytes == 8;
  // 可恢复命令让出执行权仍占容量；拒绝的命令不得执行，帧有独立名额。
  unsigned retry_calls{};
  auto retry = [&](const auto&) {
    ++retry_calls;
    return error::success;
  };
  auto rejected = executor.submit_command(retry);
  const bool backpressure = !rejected && rejected.error() == error::not_ready;
  std::uint64_t frame{};
  const auto submitted = gneiss::test_support::assign_value(
      executor.submit_frame({}, gneiss::render::frame_policy::required), frame);
  if (submitted != gneiss::error::success) {
    frame_executed.store(true);
  }
  const auto flushed = executor.flush();
  std::optional<render_command_completion> completion;
  const bool completed = (completion = executor.try_take_command_completion()).has_value();
  std::optional<gneiss::render::frame_completion> drawn;
  const bool rendered = (drawn = executor.try_take_frame_completion()).has_value();
  const auto stats = executor.query_stats();
  auto retried = executor.submit_command(retry);
  const bool retry_completed = retried && executor.flush().ok() && retry_calls == 1;
  auto retry_completion = executor.try_take_command_completion();
  const bool retry_reported = retry_completion && retried &&
                              retry_completion->sequence == *retried &&
                              retry_completion->status.ok();
  executor.stop();
  return backpressure && retry_completed && retry_reported && began && no_completion && alive &&
         submitted == gneiss::error::success && flushed == gneiss::error::success && completed &&
         completion->sequence == command && completion->status == terminal && rendered &&
         drawn->sequence == frame && drawn->status == gneiss::error::success &&
         stats.command_payload_bytes == 0 && stats.submitted_commands == 1U &&
         stats.executed_commands == 1U && retained.expired() &&
         !(completion = executor.try_take_command_completion());
}
} // namespace

int main(int argc, char** argv) {
  if (argc == 2) {
    using namespace gneiss;
    using namespace gneiss::render::detail;
    const std::string_view mode{argv[1]};
    threaded_render_executor fatal_executor;
    if (fatal_executor
            .initialize([](auto&, auto&) -> error { throw std::runtime_error("frame fixture"); })
            .failed())
      return 90;
    if (mode == "render-frame") {
      if (!fatal_executor.submit_frame({}))
        return 91;
    } else {
      if (!fatal_executor.submit_command(
              [](const auto&) -> error { throw std::runtime_error("command fixture"); }))
        return 92;
    }
    static_cast<void>(fatal_executor.flush());
    return 0;
  }
  {
    gneiss::render::detail::threaded_render_executor invalid;
    auto draw = [](auto&, auto&) { return gneiss::error::success; };
    if (invalid.initialize(draw, {.pending_frames = 0}) != gneiss::error::invalid_argument ||
        invalid.initialize(draw, {.commands = 0}) != gneiss::error::invalid_argument)
      return 30;
  }
  if (!frame_input_budget()) {
    std::fputs("帧输入预算与替换回收验证失败\n", stderr);
    return 33;
  }
  if (!replaced_frame_releases_outside_lock()) {
    std::fputs("被替换帧的锁外回收与完成顺序验证失败\n", stderr);
    return 32;
  }
  if (!command_payload_budget()) {
    std::fputs("命令共享数据预算、拒绝及回收验证失败\n", stderr);
    return 31;
  }
  if (!command_retirement_precedes_completion(gneiss::error::success) ||
      !command_retirement_precedes_completion(gneiss::error::io) ||
      !command_retirement_precedes_completion(gneiss::error::internal) ||
      !command_retirement_precedes_completion(gneiss::error::out_of_memory)) {
    std::fputs("命令资源释放前不应发布完成或归还队列容量\n", stderr);
    return 30;
  }
  if (!ordered_commands_survive_frame_replacement())
    return 93;
  if (!resumable_command_allows_frames(gneiss::error::success) ||
      !resumable_command_allows_frames(gneiss::error::io)) {
    return 24;
  }
  using namespace gneiss;
  using namespace gneiss::render::detail;
  threaded_render_executor empty_executor;
  if (empty_executor.try_take_frame_completion() || empty_executor.try_take_command_completion() ||
      empty_executor.query_command_status())
    return 25;
  gneiss::render::detail::frame_packet inline_packet;
  inline_packet.surface.width = 7U;
  inline_render_executor inline_executor(
      [](gneiss::render::detail::frame_packet& packet, gneiss::render::execution_result& output) {
        output.presented = packet.surface.width == 7U;
        if (packet.sequence == 0U) {
          return gneiss::error::invalid_state;
        }
        return gneiss::error::success;
      });
  gneiss::render::execution_result inline_result;
  if (inline_executor.submit(std::move(inline_packet), inline_result) != gneiss::error::success ||
      !inline_result.presented) {
    return 1;
  }

  std::mutex mutex;
  std::condition_variable ready;
  bool first_started = false;
  bool release_first = false;
  std::vector<std::uint32_t> executed;
  threaded_render_executor executor;
  const auto initialize_result = executor.initialize(
      [&](gneiss::render::detail::frame_packet& packet, gneiss::render::execution_result& output) {
        {
          std::unique_lock lock(mutex);
          executed.push_back(packet.surface.width);
          if (packet.surface.width == 1U) {
            first_started = true;
            ready.notify_all();
            ready.wait(lock, [&] { return release_first; });
          }
        }
        output.presented = packet.surface.width == 3U;
        output.gpu_timing_supported = true;
        if (packet.surface.width == 3U) {
          if (packet.surface.revision != 42U)
            return gneiss::error::internal;
          output.gpu_timing_valid = true;
          output.gpu_timing_sample_sequence = 7U;
          output.gpu_frame_ms = 1.25F;
          output.gpu_shadow_ms = 0.25F;
          output.gpu_opaque_ms = 0.75F;
          output.gpu_tone_mapping_ms = 0.25F;
        }
        return gneiss::error::success;
      },
      {.pending_frames = 1});
  if (initialize_result != gneiss::error::success || !executor.is_running()) {
    return 2;
  }

  gneiss::render::detail::frame_packet first;
  first.surface.width = 1U;
  std::uint64_t first_sequence = 0U;
  if (gneiss::test_support::assign_value(executor.submit_frame(std::move(first)), first_sequence) !=
      gneiss::error::success) {
    return 3;
  }
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return first_started; });
  }
  gneiss::render::detail::frame_packet second;
  second.surface.width = 2U;
  std::uint64_t second_sequence = 0U;
  gneiss::render::detail::frame_packet third;
  third.surface.width = 3U;
  third.surface.revision = 42U;
  third.capture.capture_ms = 2.5F;
  third.capture.copied_payload_bytes = 4096U;
  std::uint64_t third_sequence = 0U;
  if (gneiss::test_support::assign_value(executor.submit_frame(std::move(second)),
                                         second_sequence) != gneiss::error::success ||
      gneiss::test_support::assign_value(executor.submit_frame(std::move(third)), third_sequence) !=
          gneiss::error::success) {
    return 4;
  }
  {
    std::lock_guard lock(mutex);
    release_first = true;
    ready.notify_all();
  }
  if (executor.flush() != gneiss::error::success) {
    return 5;
  }

  std::vector<gneiss::render::frame_completion> completions;
  std::optional<gneiss::render::frame_completion> completion;
  while ((completion = executor.try_take_frame_completion())) {
    completions.push_back(std::move(*completion));
  }
  const auto stats = executor.query_stats();
  if (executed != std::vector<std::uint32_t>{1U, 3U} || completions.size() != 3U ||
      stats.replaced_frames != 1U || stats.pending_high_watermark == 0U ||
      stats.submitted_frames != 3U || stats.executed_frames != 2U || stats.presented_frames != 1U ||
      stats.pending_tasks != 0U || stats.pending_frames != 0U || stats.pending_commands != 0U ||
      stats.latest_frame_queue_wait_ms < 0.0F ||
      stats.maximum_frame_queue_wait_ms < stats.latest_frame_queue_wait_ms ||
      stats.latest_frame_capture_ms != 2.5F || stats.latest_copied_payload_bytes != 4096U ||
      !stats.gpu_timing_supported || !stats.latest_gpu_timing_valid ||
      stats.latest_gpu_timing_sample_sequence != 7U || stats.gpu_timing_sample_count != 1U ||
      stats.gpu_timing_unavailable_count != 1U || stats.latest_gpu_frame_ms != 1.25F ||
      stats.latest_gpu_shadow_ms != 0.25F || stats.latest_gpu_opaque_ms != 0.75F ||
      stats.latest_gpu_tone_mapping_ms != 0.25F) {
    return 6;
  }
  bool saw_dropped = false;
  bool saw_presented = false;
  bool saw_capture_metrics = false;
  for (const auto& item : completions) {
    saw_dropped = saw_dropped || (item.sequence == second_sequence && item.dropped &&
                                  item.status == gneiss::error::not_ready);
    saw_presented = saw_presented || (item.sequence == third_sequence && item.execution.presented);
    saw_capture_metrics = saw_capture_metrics || (item.sequence == third_sequence &&
                                                  item.execution.frame_capture_ms == 2.5F &&
                                                  item.execution.copied_payload_bytes == 4096U);
  }
  if (!saw_dropped || !saw_presented || first_sequence == second_sequence ||
      second_sequence == third_sequence || !saw_capture_metrics) {
    return 7;
  }

  bool command_started = false;
  bool release_command = false;
  std::uint64_t command_sequence = 0U;
  if (gneiss::test_support::assign_value(
          executor.submit_command([&](const render_command_reporter& reporter) {
            reporter.report(render_command_stage::uploading, 4U, 4U);
            std::unique_lock lock(mutex);
            command_started = true;
            ready.notify_all();
            ready.wait(lock, [&] { return release_command; });
            return gneiss::error::success;
          }),
          command_sequence) != gneiss::error::success) {
    return 8;
  }
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return command_started; });
  }
  std::optional<render_command_status> command_status;
  if (!(command_status = executor.query_command_status()) || !command_status->active ||
      command_status->sequence != command_sequence ||
      command_status->progress.stage != render_command_stage::uploading ||
      command_status->progress.completed_work != 4U) {
    return 9;
  }
  {
    std::lock_guard lock(mutex);
    release_command = true;
    ready.notify_all();
  }
  if (executor.flush() != gneiss::error::success) {
    return 10;
  }
  std::optional<render_command_completion> command_completion;
  if (!(command_completion = executor.try_take_command_completion()) ||
      command_completion->sequence != command_sequence ||
      command_completion->status != gneiss::error::success ||
      command_completion->progress.stage != render_command_stage::completed ||
      command_completion->progress.completed_work != 4U ||
      !(command_status = executor.query_command_status()) || command_status->active ||
      command_status->sequence != command_sequence ||
      command_status->progress.stage != render_command_stage::completed) {
    return 11;
  }
  std::uint64_t failed_sequence = 0U;
  if (gneiss::test_support::assign_value(
          executor.submit_command([](const render_command_reporter& reporter) {
            reporter.report(render_command_stage::uploading, 1U, 3U);
            return gneiss::error::invalid_state;
          }),
          failed_sequence) != gneiss::error::success ||
      executor.flush() != gneiss::error::success ||
      !(command_completion = executor.try_take_command_completion()) ||
      command_completion->sequence != failed_sequence ||
      command_completion->status != gneiss::error::invalid_state ||
      command_completion->progress.completed_work != 1U ||
      command_completion->progress.total_work != 3U || !executor.is_running()) {
    return 12;
  }
  executor.record_skipped_frame_build();
  const auto final_stats = executor.query_stats();
  if (final_stats.submitted_commands != 2U || final_stats.executed_commands != 2U ||
      final_stats.rejected_commands != 0U || command_completion->progress.total_work != 3U) {
    return 13;
  }
  if (final_stats.skipped_frame_builds != 1U) {
    return 14;
  }
  executor.stop();
  executor.stop();
  if (executor.is_running()) {
    return 15;
  }

  bool required_blocked = false;
  bool release_required = false;
  threaded_render_executor required_executor;
  if (required_executor.initialize(
          [&](gneiss::render::detail::frame_packet& packet, gneiss::render::execution_result&) {
            if (packet.surface.width == 10U) {
              std::unique_lock lock(mutex);
              required_blocked = true;
              ready.notify_all();
              ready.wait(lock, [&] { return release_required; });
            }
            if (packet.readback)
              packet.readback->width = packet.surface.width;
            return gneiss::error::success;
          },
          {.pending_frames = 1}) != gneiss::error::success) {
    return 16;
  }
  gneiss::render::detail::frame_packet blocking_packet;
  blocking_packet.surface.width = 10U;
  std::uint64_t blocking_sequence{};
  if (gneiss::test_support::assign_value(required_executor.submit_frame(std::move(blocking_packet)),
                                         blocking_sequence) != gneiss::error::success) {
    return 16;
  }
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return required_blocked; });
  }
  gneiss::render::detail::frame_packet required_packet;
  required_packet.surface.width = 20U;
  auto captured_image = std::make_shared<gneiss::render::frame_image>();
  required_packet.readback = captured_image;
  std::uint64_t required_sequence{};
  if (gneiss::test_support::assign_value(
          required_executor.submit_frame(std::move(required_packet),
                                         gneiss::render::frame_policy::required),
          required_sequence) != gneiss::error::success) {
    return 17;
  }
  gneiss::render::detail::frame_packet replaceable_packet;
  replaceable_packet.surface.width = 30U;
  std::uint64_t rejected_sequence{};
  if (gneiss::test_support::assign_value(
          required_executor.submit_frame(std::move(replaceable_packet)), rejected_sequence) !=
      gneiss::error::not_ready) {
    return 18;
  }
  gneiss::render::detail::frame_packet second_required_packet;
  second_required_packet.surface.width = 40U;
  if (gneiss::test_support::assign_value(
          required_executor.submit_frame(std::move(second_required_packet),
                                         gneiss::render::frame_policy::required),
          rejected_sequence) != gneiss::error::not_ready) {
    return 19;
  }
  {
    std::lock_guard lock(mutex);
    release_required = true;
    ready.notify_all();
  }
  if (required_executor.flush() != gneiss::error::success) {
    return 20;
  }
  bool captured_completion = false;
  while (auto completed = required_executor.try_take_frame_completion()) {
    if (completed->sequence == required_sequence) {
      if (completed->dropped || completed->status.failed() || captured_completion ||
          captured_image->width != 20U) {
        std::fputs("必达截图应只完成一次且返回执行结果\n", stderr);
        return 20;
      }
      captured_completion = true;
    }
  }
  if (!captured_completion)
    return 20;
  const auto required_stats = required_executor.query_stats();
  if (required_stats.submitted_required_frames != 1U ||
      required_stats.executed_required_frames != 1U ||
      required_stats.rejected_required_frames != 1U || required_stats.replaced_frames != 0U ||
      required_sequence == 0U) {
    return 21;
  }
  auto signal = std::make_shared<gneiss::app::main_loop_wakeup>();
  required_executor.set_completion_wakeup(signal);
  const auto observed = signal->snapshot();
  std::uint64_t notified_sequence{};
  if (gneiss::test_support::assign_value(
          required_executor.submit_command([](const auto&) { return gneiss::error::internal; }),
          notified_sequence) != gneiss::error::success ||
      !signal->wait(observed, std::chrono::seconds(2))) {
    return 22;
  }
  std::optional<render_command_completion> notified;
  if (!(notified = required_executor.try_take_command_completion()) ||
      notified->sequence != notified_sequence || notified->status != gneiss::error::internal) {
    return 23;
  }
  required_executor.stop();
  threaded_render_executor once_executor;
  unsigned calls{};
  std::uint64_t once_sequence{};
  if (once_executor.initialize([](auto&, auto&) { return gneiss::error::success; }) !=
          gneiss::error::success ||
      gneiss::test_support::assign_value(once_executor.submit_command([&](const auto&) {
        ++calls;
        return gneiss::error::not_ready;
      }),
                                         once_sequence) != gneiss::error::success ||
      once_executor.flush() != gneiss::error::success) {
    return 25;
  }
  std::optional<render_command_completion> once;
  if (!(once = once_executor.try_take_command_completion()) || calls != 1U ||
      once->status != gneiss::error::not_ready || once->sequence != once_sequence) {
    return 26;
  }
  threaded_render_executor failed_prepare;
  unsigned draw_calls{};
  if (failed_prepare.initialize([&](auto&, auto&) {
        ++draw_calls;
        return gneiss::error::success;
      }) != gneiss::error::success)
    return 27;
  auto prepare_sequence =
      failed_prepare.submit_frame({}, gneiss::render::frame_policy::required,
                                  []() -> gneiss::result<gneiss::render::detail::frame_packet> {
                                    return gneiss::error::unsupported;
                                  });
  if (!prepare_sequence || failed_prepare.flush() != gneiss::error::success)
    return 28;
  auto failure = failed_prepare.try_take_frame_completion();
  if (!failure || failure->sequence != *prepare_sequence ||
      failure->status != gneiss::error::unsupported || failure->dropped || draw_calls != 0)
    return 29;
  return 0;
}
