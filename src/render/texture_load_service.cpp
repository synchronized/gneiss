// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/texture_load_service.hpp"
#include "engine/asset/asset_uri.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace gneiss::render_internal {
struct texture_load_service::pending {
  struct cpu_result {
    prepared_render_batch batch;
    gneiss_result result{GNEISS_ERROR_INTERNAL};
    std::string message;
    double milliseconds{};
  };
  std::shared_ptr<cpu_result> cpu{std::make_shared<cpu_result>()};
  std::vector<render_asset_loader::asset_candidate> candidates;
  texture_upload_backend::data data;
  render_asset_loader::revision_stamp observed;
  texture_load_completion completion;
  tasks::task_handle task;
  bool prepared{};
  bool uploading{};
  bool in_flight{};
  bool discarding{};
  bool cancelled{};
  std::size_t next_upload{};
  std::size_t completed_uploads{};
  std::uint64_t upload{};
  std::size_t upload_reserved_bytes{};
  gneiss_result failure{GNEISS_SUCCESS};
  std::chrono::steady_clock::time_point commit_started;
};
texture_load_service::texture_load_service(tasks::task_executor& executor,
                                           asset_internal::virtual_file_system file_system,
                                           render_asset_loader& loader,
                                           texture_upload_backend backend)
    : executor_(executor), scope_(executor.make_scope()), file_system_(std::move(file_system)),
      loader_(loader), backend_(std::move(backend)) {
  if (scope_.id == 0U || !backend_.begin || !backend_.poll || !backend_.discard ||
      !backend_.flush) {
    if (scope_.id != 0U) {
      (void)executor_.close_scope(scope_);
    }
    throw std::invalid_argument("资产服务执行或上传后端无效");
  }
}
texture_load_service::~texture_load_service() {
  try {
    request_stop();
    (void)executor_.close_scope(scope_);
    if (pending_ && !pending_->uploading) {
      pending_.reset();
    }
    while (pending_) {
      backend_.flush();
      advance();
    }
  } catch (...) {
    std::terminate();
  }
}
gneiss_result texture_load_service::submit(std::span<const std::string> uris, std::uint64_t session,
                                           std::uint64_t revision, std::uint64_t& request,
                                           bool reload) {
  request = 0U;
  if (uris.size() > maximum_batch) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  std::vector<render_asset_reload> sources;
  for (const auto& uri : uris) {
    sources.push_back({uri, render_asset_type::texture});
  }
  return submit_assets(sources, session, revision, request, reload);
}
gneiss_result texture_load_service::submit_assets(std::span<const render_asset_reload> sources,
                                                  std::uint64_t session, std::uint64_t revision,
                                                  std::uint64_t& request, bool reload,
                                                  std::size_t prepare_limit) {
  request = 0U;
  if (std::this_thread::get_id() != owner_ || stopping_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  if (pending_ || completed_) {
    return GNEISS_ERROR_NOT_READY;
  }
  if (sources.empty() || sources.size() > maximum_assets || session == 0U || revision == 0U ||
      prepare_limit == 0U || prepare_limit > maximum_candidate_bytes) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  std::set<std::string> unique;
  for (const auto& source : sources) {
    if (asset_internal::validate_uri(source.uri) != GNEISS_SUCCESS ||
        !unique.insert(source.uri).second ||
        (source.type != render_asset_type::mesh && source.type != render_asset_type::material &&
         source.type != render_asset_type::texture)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  auto value = std::make_unique<pending>();
  value->observed = loader_.revision();
  value->completion.request = ++sequence_;
  value->completion.session = session;
  value->completion.revision = revision;
  value->candidates.reserve(maximum_assets);
  value->data.reserve(maximum_assets);
  value->completion.textures.reserve(maximum_assets);
  value->completion.assets.reserve(maximum_assets);
  if (!reload) {
    bool cached = true;
    for (const auto& source : sources) {
      render_asset_lease lease;
      if (loader_.acquire_cached(source, lease, backend_.profile) != GNEISS_SUCCESS) {
        cached = false;
        break;
      }
      value->completion.assets.push_back(lease);
      if (lease.type() == render_asset_type::texture) {
        value->completion.textures.push_back(loader_.texture_lease(lease));
      }
    }
    if (cached) {
      value->completion.state = texture_load_state::applied;
      value->completion.result = GNEISS_SUCCESS;
      request = value->completion.request;
      completed_ = std::move(value->completion);
      return GNEISS_SUCCESS;
    }
    value->completion.assets.clear();
    value->completion.textures.clear();
  }
  const auto accepted = executor_.submit(
      {.name = "render_assets.prepare", .scope = scope_},
      [cpu = value->cpu, sources = std::vector<render_asset_reload>(sources.begin(), sources.end()),
       files = file_system_, prepare_limit,
       profile = backend_.profile](const tasks::task_context& context) {
        const auto start = std::chrono::steady_clock::now();
        asset_diagnostic diagnostic;
        cpu->result = prepare_render_assets(
            files, sources, cpu->batch, diagnostic, [&] { return context.stop_requested(); },
            maximum_assets, prepare_limit, profile);
        cpu->message = std::move(diagnostic.message);
        cpu->milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        return tasks::task_outcome{.state = context.stop_requested() ? tasks::task_state::cancelled
                                            : cpu->result == GNEISS_SUCCESS
                                                ? tasks::task_state::succeeded
                                                : tasks::task_state::failed};
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
  pending_->completion.candidate_bytes = pending_->cpu->batch.bytes;
  completed_ = std::move(pending_->completion);
  pending_.reset();
}
void texture_load_service::check_owner() const {
  if (std::this_thread::get_id() != owner_) {
    throw std::logic_error("资产服务线程错误");
  }
}
void texture_load_service::advance() {
  check_owner();
  try {
    advance_impl();
  } catch (const std::bad_alloc&) {
    if (pending_) {
      if (pending_->uploading) {
        pending_->failure = GNEISS_ERROR_OUT_OF_MEMORY;
      } else {
        finish(GNEISS_ERROR_OUT_OF_MEMORY, texture_load_state::failed);
      }
    }
  } catch (...) {
    if (pending_) {
      if (pending_->uploading) {
        pending_->failure = GNEISS_ERROR_INTERNAL;
      } else {
        finish(GNEISS_ERROR_INTERNAL, texture_load_state::failed);
      }
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
    if (value.observed != loader_.revision()) {
      finish(GNEISS_ERROR_INVALID_STATE, texture_load_state::failed);
      return;
    }
    const auto start = std::chrono::steady_clock::now();
    for (unsigned count = 0U;
         value.candidates.size() < value.cpu->batch.assets.size() && count < 4U; ++count) {
      render_asset_loader::asset_candidate candidate;
      const auto result = loader_.stage_asset(
          std::move(value.cpu->batch.assets[value.candidates.size()]), value.candidates, candidate);
      if (result != GNEISS_SUCCESS) {
        finish(result, texture_load_state::failed);
        return;
      }
      render_upload_item upload{candidate.mesh,    candidate.material,
                                candidate.texture, candidate.dependency_textures,
                                candidate.bytes,   candidate.texture_payload};
      if (backend_.estimate_bytes)
        upload.bytes = backend_.estimate_bytes(upload);
      value.data.push_back(std::move(upload));
      value.candidates.push_back(std::move(candidate));
      if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(2)) {
        break;
      }
    }
    if (value.candidates.size() != value.cpu->batch.assets.size()) {
      return;
    }
  }
  if (value.in_flight) {
    gneiss_result result{};
    if (!backend_.poll(value.upload, result)) {
      return;
    }
    value.in_flight = false;
    value.upload_reserved_bytes = 0U;
    if (value.discarding) {
      finish(value.failure, texture_load_state::failed);
      return;
    }
    if (result != GNEISS_SUCCESS) {
      if (value.completed_uploads == 0U) {
        finish(result, texture_load_state::failed);
        return;
      }
      value.failure = result;
    } else {
      value.completed_uploads = value.next_upload;
      if (backend_.elapsed_ms) {
        value.completion.upload_ms += backend_.elapsed_ms();
      }
    }
  }
  if (value.failure != GNEISS_SUCCESS) {
    const auto result = backend_.discard(value.data, value.upload);
    if (result == GNEISS_ERROR_NOT_READY) {
      return;
    }
    if (result != GNEISS_SUCCESS) {
      finish(result, texture_load_state::failed);
      return;
    }
    value.discarding = true;
    value.in_flight = true;
    return;
  }
  if (value.next_upload < value.data.size()) {
    texture_upload_backend::data batch;
    std::size_t bytes{};
    auto end = value.next_upload;
    while (end < value.data.size() && batch.size() < 4U) {
      const auto& item = value.data[end];
      if (item.bytes > maximum_upload_bytes) {
        value.cpu->message = "GPU 上传预算不足：需要 " + std::to_string(item.bytes) +
                             " 字节，上限 " + std::to_string(maximum_upload_bytes);
        if (value.completed_uploads != 0U) {
          value.failure = GNEISS_ERROR_OUT_OF_MEMORY;
        } else {
          finish(GNEISS_ERROR_OUT_OF_MEMORY, texture_load_state::failed);
        }
        return;
      }
      if (!batch.empty() &&
          item.bytes > upload_budget_bytes - std::min(bytes, upload_budget_bytes)) {
        break;
      }
      bytes += item.bytes;
      batch.push_back(item);
      ++end;
    }
    const auto result = backend_.begin(std::move(batch), value.upload);
    if (result == GNEISS_ERROR_NOT_READY) {
      return;
    }
    if (result != GNEISS_SUCCESS) {
      if (value.uploading) {
        value.failure = result;
      } else {
        finish(result, texture_load_state::failed);
      }
      return;
    }
    value.upload_reserved_bytes = bytes;
    value.completion.peak_upload_bytes = std::max(value.completion.peak_upload_bytes, bytes);
    value.uploading = true;
    value.in_flight = true;
    value.next_upload = end;
    return;
  }
  auto result = value.observed == loader_.revision() ? loader_.publish_assets(value.candidates)
                                                     : GNEISS_ERROR_INVALID_STATE;
  if (result != GNEISS_SUCCESS) {
    value.failure = result;
    return;
  }
  for (const auto& candidate : value.candidates) {
    value.completion.assets.push_back(candidate.lease);
    if (candidate.texture) {
      value.completion.textures.push_back(loader_.texture_lease(candidate.lease));
    }
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
bool texture_load_service::cancel() {
  check_owner();
  if (pending_ && !pending_->uploading) {
    pending_->cancelled = true;
    (void)executor_.cancel(pending_->task);
    return true;
  }
  return false;
}
void texture_load_service::request_stop() {
  stopping_ = true;
  (void)cancel();
}
bool texture_load_service::stopped() const { return stopping_ && !pending_; }
bool texture_load_service::progress(asset_load_progress& output) const {
  check_owner();
  output = {};
  if (!pending_) {
    return false;
  }
  output = {
      pending_->completion.request,
      pending_->completion.session,
      pending_->completion.revision,
      pending_->uploading ? texture_load_state::uploading : texture_load_state::preparing,
      pending_->completed_uploads,
      pending_->prepared ? pending_->cpu->batch.assets.size() : 0U,
      !pending_->uploading,
      pending_->upload_reserved_bytes,
  };
  return true;
}
} // namespace gneiss::render_internal
