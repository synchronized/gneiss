// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/texture_load_service.h"
#include "asset/asset_uri.h"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace gneiss::asset_internal {
using namespace render_internal;
struct texture_load_service::pending {
  struct cpu_result {
    std::vector<texture_resource> data;
    gneiss_result result{GNEISS_ERROR_INTERNAL};
    std::string message{};
    std::size_t bytes{};
    double milliseconds{};
  };
  std::shared_ptr<cpu_result> cpu{std::make_shared<cpu_result>()};
  std::vector<render_asset_loader::texture_target> targets;
  std::vector<render_asset_loader::texture_candidate> candidates;
  texture_load_completion completion;
  tasks::task_handle task;
  bool prepared{};
  bool uploading{};
  bool discarding{};
  bool cancelled{};
  std::uint64_t upload{};
  gneiss_result failure{GNEISS_SUCCESS};
  std::chrono::steady_clock::time_point commit_started;
};

texture_load_service::texture_load_service(tasks::task_executor& executor,
                                           virtual_file_system file_system,
                                           render_asset_loader& loader,
                                           texture_upload_backend backend)
    : executor_(executor), scope_(executor.make_scope()), file_system_(std::move(file_system)),
      loader_(loader), backend_(std::move(backend)) {
  if (scope_.id == 0U || !backend_.begin || !backend_.poll || !backend_.discard ||
      !backend_.flush) {
    if (scope_.id != 0U) {
      (void)executor_.close_scope(scope_);
    }
    throw std::invalid_argument("纹理服务执行或上传后端无效");
  }
}
texture_load_service::~texture_load_service() {
  try {
    request_stop();
    (void)executor_.close_scope(scope_);
    // 桌面析构为最终等待边界；交互关闭先逐帧 request_stop/advance，不在帧内等待。
    if (pending_ && !pending_->uploading) {
      pending_.reset();
    }
    while (pending_) {
      backend_.flush();
      advance();
    }

  } catch (...) {
    // 所属线程或执行器生命周期契约被破坏时，不能释放仍可能运行的服务状态。
    std::terminate();
  }
}

gneiss_result texture_load_service::submit(std::span<const std::string> uris, std::uint64_t session,
                                           std::uint64_t revision, std::uint64_t& request,
                                           bool reload) {
  request = 0U;
  if (std::this_thread::get_id() != owner_ || stopping_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  if (pending_ || completed_) {
    return GNEISS_ERROR_NOT_READY;
  }
  if (uris.empty() || uris.size() > maximum_batch || session == 0U || revision == 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto value = std::make_unique<pending>();
  std::set<std::string> unique;
  for (const auto& uri : uris) {
    if (validate_uri(uri) != GNEISS_SUCCESS || !unique.insert(uri).second) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    render_asset_loader::texture_target target;
    const auto observed = loader_.observe_texture(uri, target);
    if (observed != GNEISS_SUCCESS) {
      return observed;
    }
    value->targets.push_back(std::move(target));
  }
  value->completion.request = ++sequence_;
  value->completion.session = session;
  value->completion.revision = revision;
  value->candidates.reserve(uris.size());
  value->completion.textures.reserve(uris.size());
  if (!reload &&
      std::ranges::all_of(value->targets, [](const auto& target) { return target.existed; })) {
    for (const auto& target : value->targets) {
      texture_asset_lease lease;
      asset_diagnostic diagnostic;
      const auto acquired = loader_.acquire_texture(target.uri, lease, diagnostic);
      if (acquired != GNEISS_SUCCESS) {
        return acquired;
      }
      value->completion.textures.push_back(std::move(lease));
    }
    value->completion.state = texture_load_state::applied;
    value->completion.result = GNEISS_SUCCESS;
    request = value->completion.request;
    completed_ = std::move(value->completion);
    return GNEISS_SUCCESS;
  }
  const auto accepted = executor_.submit(
      {.name = "texture.prepare", .scope = scope_},
      [cpu = value->cpu, sources = std::vector<std::string>(uris.begin(), uris.end()),
       file_system = file_system_](const tasks::task_context& context) {
        const auto start = std::chrono::steady_clock::now();
        for (const auto& uri : sources) {
          if (context.stop_requested()) {
            return tasks::task_outcome{tasks::task_state::cancelled, {}};
          }
          texture_resource data;
          asset_diagnostic diagnostic;
          cpu->result = prepare_texture(file_system, uri, data, diagnostic, maximum_bytes, true,
                                        maximum_bytes - cpu->bytes);
          if (cpu->result != GNEISS_SUCCESS) {
            cpu->message = std::move(diagnostic.message);
            break;
          }
          std::size_t bytes = data.manifest.size() + data.payload.size();
          for (const auto& mip : data.levels) {
            bytes += mip.pixels.size();
          }
          if (bytes > maximum_bytes - cpu->bytes) {
            cpu->result = GNEISS_ERROR_INVALID_ARGUMENT;
            break;
          }
          cpu->bytes += bytes;
          cpu->data.push_back(std::move(data));
        }
        cpu->milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        return tasks::task_outcome{cpu->result == GNEISS_SUCCESS ? tasks::task_state::succeeded
                                                                 : tasks::task_state::failed,
                                   {}};
      },
      value->task);
  if (accepted != tasks::submit_result::success) {
    return accepted == tasks::submit_result::full ? GNEISS_ERROR_NOT_READY
                                                  : GNEISS_ERROR_INVALID_STATE;
  }
  request = value->completion.request;
  pending_ = std::move(value);
  return GNEISS_SUCCESS;
}
void texture_load_service::finish(gneiss_result result, texture_load_state state) {
  pending_->completion.result = result;
  pending_->completion.message = std::move(pending_->cpu->message);
  pending_->completion.state = state;
  pending_->completion.prepare_ms = pending_->cpu->milliseconds;
  pending_->completion.candidate_bytes = pending_->cpu->bytes;
  completed_ = std::move(pending_->completion);
  pending_.reset();
}
void texture_load_service::check_owner() const {
  if (std::this_thread::get_id() != owner_) {
    throw std::logic_error("纹理服务线程错误");
  }
}
void texture_load_service::advance() {
  check_owner();
  try {
    advance_impl();
  } catch (const std::bad_alloc&) {
    if (pending_) {
      finish(GNEISS_ERROR_OUT_OF_MEMORY, texture_load_state::failed);
    }
  } catch (...) {
    if (pending_) {
      finish(GNEISS_ERROR_INTERNAL, texture_load_state::failed);
    }
  }
}
void texture_load_service::advance_impl() {
  if (!pending_) {
    return;
  }
  auto& value = *pending_;
  if (!value.prepared) {
    std::vector<tasks::task_completion> results;
    if (executor_.poll(scope_, results, 1U) == 0U) {
      return;
    }
    if (value.cancelled || results.front().outcome.state == tasks::task_state::cancelled) {
      finish(GNEISS_ERROR_INVALID_STATE, texture_load_state::cancelled);
      return;
    }
    if (results.front().outcome.state != tasks::task_state::succeeded ||
        value.cpu->result != GNEISS_SUCCESS) {
      finish(value.cpu->result == GNEISS_SUCCESS ? GNEISS_ERROR_INTERNAL : value.cpu->result,
             texture_load_state::failed);
      return;
    }
    value.completion.queue_ms = results.front().queue_ms;
    value.prepared = true;
    value.commit_started = std::chrono::steady_clock::now();
  }
  if (!value.uploading) {
    if (value.cancelled) {
      finish(GNEISS_ERROR_INVALID_STATE, texture_load_state::cancelled);
      return;
    }
    const auto start = std::chrono::steady_clock::now();
    for (unsigned count = 0U; value.candidates.size() < value.targets.size() && count < 4U;
         ++count) {
      const auto index = value.candidates.size();
      render_asset_loader::texture_candidate candidate;
      const auto result =
          loader_.stage_texture(value.targets[index], std::move(value.cpu->data[index]), candidate);
      if (result != GNEISS_SUCCESS) {
        finish(result, texture_load_state::failed);
        return;
      }
      value.candidates.push_back(std::move(candidate));
      if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(2)) {
        break;
      }
    }
    if (value.candidates.size() != value.targets.size()) {
      return;
    }
    texture_upload_backend::data data;
    for (const auto& candidate : value.candidates) {
      data.push_back(candidate.data);
    }
    const auto result = backend_.begin(std::move(data), value.upload);
    if (result == GNEISS_ERROR_NOT_READY) {
      return;
    }
    if (result != GNEISS_SUCCESS) {
      finish(result, texture_load_state::failed);
      return;
    }
    value.uploading = true;
    return;
  }
  if (value.failure != GNEISS_SUCCESS && !value.discarding) {
    texture_upload_backend::data data;
    for (const auto& candidate : value.candidates) {
      data.push_back(candidate.data);
    }
    const auto result = backend_.discard(std::move(data), value.upload);
    if (result == GNEISS_ERROR_NOT_READY) {
      return;
    }
    if (result != GNEISS_SUCCESS) {
      finish(result, texture_load_state::failed);
      return;
    }
    value.discarding = true;
  }
  gneiss_result result{};
  if (!backend_.poll(value.upload, result)) {
    return;
  }
  if (value.discarding) {
    finish(value.failure, texture_load_state::failed);
    return;
  }
  if (result != GNEISS_SUCCESS) {
    finish(result, texture_load_state::failed);
    return;
  }
  value.completion.upload_ms = backend_.elapsed_ms ? backend_.elapsed_ms() : 0.0;
  result = loader_.publish_textures(value.candidates);
  if (result != GNEISS_SUCCESS) {
    value.failure = result;
    return;
  }
  for (const auto& candidate : value.candidates) {
    value.completion.textures.push_back(candidate.lease);
  }
  value.completion.commit_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - value.commit_started)
                                   .count();
  finish(GNEISS_SUCCESS, texture_load_state::applied);
}
bool texture_load_service::take(texture_load_completion& output) {
  check_owner();
  if (!completed_) {
    return false;
  }
  output = std::move(*completed_);
  completed_.reset();
  return true;
}
void texture_load_service::cancel() {
  check_owner();
  if (pending_ && !pending_->uploading) {
    pending_->cancelled = true;
    (void)executor_.cancel(pending_->task);
  }
}
void texture_load_service::request_stop() {
  stopping_ = true;
  cancel();
}
bool texture_load_service::stopped() const { return stopping_ && !pending_; }
} // namespace gneiss::asset_internal
