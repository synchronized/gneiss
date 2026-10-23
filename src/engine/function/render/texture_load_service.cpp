// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/texture_load_service.hpp"
#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/diagnostics/task_submission.hpp"
#include "engine/resource/cache/cpu_document_cache.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace gneiss::engine::render {
struct texture_load_service::pending {
  std::uint64_t update_sequence{};
  struct cpu_result {
    prepared_render_batch batch;
    prepared_render_batch current;
    std::vector<render_asset_reload> sources;
    std::vector<asset_load_result> failures;
    std::size_t cursor{};
    std::size_t limit{};
    engine::resource::material_document_decoder decode;
    engine::resource::virtual_file_system files;
    texture_prepare_profile profile{};
    std::shared_ptr<engine::resource::cpu_document_batch> documents;
    std::unique_ptr<engine::resource::asset_preparation> preparation;
    bool complete{};
    gneiss_result result{GNEISS_ERROR_INTERNAL};
    std::string message;
    double milliseconds{};
  };
  std::shared_ptr<cpu_result> cpu{std::make_shared<cpu_result>()};
  std::vector<render_asset_loader::asset_candidate> candidates;
  std::vector<render_asset_lease> targets;
  texture_upload_backend::data data;
  texture_load_completion completion;
  engine::core::tasks::task_handle task;
  bool prepared{};
  bool uploading{};
  bool in_flight{};
  bool discarding{};
  bool discard_item{};
  bool cancelled{};
  std::size_t staged_count{};
  gneiss_result summary{GNEISS_SUCCESS};
  std::size_t next_upload{};
  std::size_t completed_uploads{};
  std::size_t processed_uploads{};
  std::uint64_t upload{};
  std::size_t upload_reserved_bytes{};
  gneiss_result failure{GNEISS_SUCCESS};
  std::chrono::steady_clock::time_point commit_started;
};
texture_load_service::texture_load_service(
    engine::core::tasks::task_executor& executor, engine::resource::virtual_file_system file_system,
    render_asset_loader& loader, texture_upload_backend backend,
    std::shared_ptr<engine::core::progress_notification> notification,
    std::shared_ptr<engine::resource::cpu_document_cache> documents)
    : executor_(executor), scope_(executor.make_scope()), file_system_(std::move(file_system)),
      loader_(loader), backend_(std::move(backend)), notification_(std::move(notification)),
      documents_(std::move(documents)) {
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
    if (engine::resource::validate_uri(source.uri) != GNEISS_SUCCESS ||
        !unique.insert(source.uri).second ||
        (source.type != render_asset_type::mesh && source.type != render_asset_type::material &&
         source.type != render_asset_type::texture)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  auto value = std::make_unique<pending>();
  value->completion.request = ++sequence_;
  value->completion.session = session;
  value->completion.revision = revision;
  value->completion.profile = backend_.profile;
  value->candidates.reserve(maximum_assets);
  value->data.reserve(maximum_assets);
  value->completion.assets.reserve(maximum_assets);
  value->completion.results.reserve(maximum_assets);
  for (const auto& source : sources) {
    value->completion.results.push_back({.source = source});
  }
  if (!reload) {
    bool cached = true;
    for (const auto& source : sources) {
      render_asset_lease lease;
      if (loader_.acquire_cached(source, lease, backend_.profile) != GNEISS_SUCCESS) {
        cached = false;
        break;
      }
      value->completion.assets.push_back(lease);
    }
    if (cached) {
      for (auto& item : value->completion.results) {
        item.result = GNEISS_SUCCESS;
        item.applied = true;
      }
      value->completion.state = texture_load_state::applied;
      value->completion.result = GNEISS_SUCCESS;
      request = value->completion.request;
      completed_ = std::move(value->completion);
      return GNEISS_SUCCESS;
    }
    value->completion.assets.clear();
  }
  engine::resource::material_document_decoder decode;
  {
    value->update_sequence = loader_.next_update_sequence();
    value->targets.reserve(sources.size());
    for (const auto& source : sources) {
      render_asset_lease target;
      const auto reserved = loader_.reserve_asset(source, target);
      if (reserved != GNEISS_SUCCESS) {
        value.reset();
        loader_.release_unused();
        return reserved;
      }
      value->targets.push_back(std::move(target));
    }
  }
  if (documents_) {
    value->cpu->documents = documents_->begin();
    decode = [documents = value->cpu->documents](const auto& bytes, auto& parameters, auto& uris,
                                                 auto& diagnostic) {
      return documents->decode(bytes, parameters, uris, diagnostic);
    };
  }
  value->cpu->sources.assign(sources.begin(), sources.end());
  value->cpu->limit = prepare_limit;
  value->cpu->files = file_system_;
  value->cpu->profile = backend_.profile;
  value->cpu->decode = std::move(decode);
  const auto accepted =
      engine::core::diagnostics::measure(engine::core::diagnostics::loop_stage::task_submit,
                                         [&] { return submit_preparation(*value); });
  if (accepted != engine::core::tasks::submit_result::success) {
    value.reset();
    loader_.release_unused();
    return accepted == engine::core::tasks::submit_result::full ? GNEISS_ERROR_NOT_READY
                                                                : GNEISS_ERROR_INVALID_STATE;
  }
  // 任务被接受后才取代旧请求；队列满等提交失败不会使旧任务失效。
  for (const auto& target : value->targets) {
    (void)loader_.claim_update(target, value->update_sequence);
  }
  request = value->completion.request;
  pending_ = std::move(value);
  return GNEISS_SUCCESS;
}
engine::core::tasks::submit_result texture_load_service::submit_preparation(pending& value) {
  return engine::core::diagnostics::submit_observed(
      executor_, {.name = "render_assets.prepare", .scope = scope_, .notification = notification_},
      [cpu = value.cpu](const engine::core::tasks::task_context& context) {
        GNEISS_PROFILE_SCOPE("asset.prepare.step");
        const auto start = std::chrono::steady_clock::now();
        asset_diagnostic diagnostic;
        // 协作任务与主线程共用执行时间，单块预算不能沿用后台吞吐批次。
        const auto byte_budget =
            std::size_t{context.allows_blocking_wait() ? 16U : 4U} * 1024U * 1024U;
        if (!cpu->preparation) {
          cpu->preparation = std::make_unique<engine::resource::asset_preparation>(
              cpu->files, std::span(cpu->sources).subspan(cpu->cursor, 1U),
              engine::resource::asset_preparation::limits{
                  .material_bytes = sizeof(material_resource),
                  .maximum_assets = maximum_assets,
                  .maximum_bytes = cpu->limit - std::min(cpu->batch.bytes, cpu->limit)},
              cpu->profile, cpu->decode);
        }
        bool item_complete{};
        const auto prepared = cpu->preparation->advance(
            byte_budget, [&] { return context.stop_requested(); }, cpu->current, diagnostic,
            item_complete, std::chrono::milliseconds(context.allows_blocking_wait() ? 4 : 0));
        cpu->result = GNEISS_SUCCESS;
        if (item_complete) {
          if (prepared != GNEISS_SUCCESS) {
            cpu->failures.push_back({cpu->sources[cpu->cursor], prepared, false});
            cpu->message = std::move(diagnostic.message);
          } else {
            const auto additional =
                std::ranges::count_if(cpu->current.assets, [&](const auto& asset) {
                  return std::ranges::none_of(cpu->batch.assets, [&](const auto& previous) {
                    return previous.source.uri == asset.source.uri;
                  });
                });
            const bool conflicting =
                std::ranges::any_of(cpu->current.assets, [&](const auto& asset) {
                  return std::ranges::any_of(cpu->batch.assets, [&](const auto& previous) {
                    return previous.source.uri == asset.source.uri &&
                           previous.source.type != asset.source.type;
                  });
                });
            if (conflicting ||
                static_cast<std::size_t>(additional) > maximum_assets - cpu->batch.assets.size()) {
              cpu->failures.push_back(
                  {cpu->sources[cpu->cursor], GNEISS_ERROR_INVALID_ARGUMENT, false});
              cpu->current = {};
            }
            for (auto& asset : cpu->current.assets) {
              if (std::ranges::none_of(cpu->batch.assets, [&](const auto& previous) {
                    return previous.source.uri == asset.source.uri;
                  })) {
                cpu->batch.bytes += asset.bytes;
                cpu->batch.assets.push_back(std::move(asset));
              }
            }
          }
          cpu->current = {};
          cpu->preparation.reset();
          cpu->complete = ++cpu->cursor == cpu->sources.size();
          if (cpu->complete) {
            std::ranges::stable_sort(cpu->batch.assets, {}, [](const auto& asset) {
              return asset.source.type == render_asset_type::material ? 1U : 0U;
            });
          }
        }
        cpu->milliseconds +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        if (context.stop_requested()) {
          return engine::core::tasks::task_outcome{.state =
                                                       engine::core::tasks::task_state::cancelled};
        }
        return engine::core::tasks::task_outcome{
            .state = cpu->result == GNEISS_SUCCESS ? engine::core::tasks::task_state::succeeded
                                                   : engine::core::tasks::task_state::failed,
        };
      },
      value.task);
}
void texture_load_service::finish(gneiss_result result, texture_load_state state) {
  const engine::core::diagnostics::loop_span span(
      engine::core::diagnostics::loop_stage::candidate_cleanup);
  pending_->completion.result = result;
  pending_->completion.message = std::move(pending_->cpu->message);
  pending_->completion.state = state;
  pending_->completion.prepare_ms = pending_->cpu->milliseconds;
  pending_->completion.candidate_bytes = pending_->cpu->batch.bytes;
  for (auto& item : pending_->completion.results) {
    if (!item.applied && item.result == GNEISS_ERROR_NOT_READY) {
      item.result = result;
      item.applied = result == GNEISS_SUCCESS;
    }
  }

  for (const auto& value : pending_->candidates) {
    const auto item = std::ranges::find_if(pending_->completion.results, [&](const auto& entry) {
      return entry.source.uri == value.source.uri;
    });
    if (item != pending_->completion.results.end() && item->applied) {
      pending_->completion.assets.push_back(value.lease);
    }
  }
  completed_ = std::move(pending_->completion);
  if (notification_) {
    notification_->notify();
  }
  pending_.reset();
  loader_.release_unused();
}
void texture_load_service::check_owner() const {
  if (std::this_thread::get_id() != owner_) {
    throw std::logic_error("资产服务线程错误");
  }
}
std::span<const render_asset_lease> texture_load_service::requested_assets() const {
  check_owner();
  if (pending_) {
    return pending_->targets;
  }
  return completed_ ? std::span<const render_asset_lease>{completed_->assets}
                    : std::span<const render_asset_lease>{};
}
void texture_load_service::advance() {
  GNEISS_PROFILE_SCOPE("asset.advance");
  const engine::core::diagnostics::loop_span span(
      engine::core::diagnostics::loop_stage::texture_advance);
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
bool texture_load_service::advance_preparation(pending& value) {
  if (value.task.id != 0U) {
    std::vector<engine::core::tasks::task_completion> results;
    if (engine::core::diagnostics::measure(engine::core::diagnostics::loop_stage::task_collect,
                                           [&] { return executor_.poll(scope_, results, 1U); }) ==
        0U) {
      return false;
    }
    value.task = {};
    if (value.cancelled ||
        results.front().outcome.state == engine::core::tasks::task_state::cancelled) {
      finish(GNEISS_ERROR_INVALID_STATE, texture_load_state::cancelled);
      return false;
    }
    if (results.front().outcome.state != engine::core::tasks::task_state::succeeded ||
        value.cpu->result != GNEISS_SUCCESS) {
      finish(value.cpu->result == GNEISS_SUCCESS ? GNEISS_ERROR_INTERNAL : value.cpu->result,
             texture_load_state::failed);
      return false;
    }
    value.completion.queue_ms += results.front().queue_ms;
  }
  if (value.cancelled) {
    finish(GNEISS_ERROR_INVALID_STATE, texture_load_state::cancelled);
    return false;
  }
  if (!value.cpu->complete) {
    const auto accepted =
        engine::core::diagnostics::measure(engine::core::diagnostics::loop_stage::task_submit,
                                           [&] { return submit_preparation(value); });
    if (accepted != engine::core::tasks::submit_result::success &&
        accepted != engine::core::tasks::submit_result::full) {
      finish(GNEISS_ERROR_INVALID_STATE, texture_load_state::failed);
    }
    return false;
  }
  for (const auto& failure : value.cpu->failures) {
    auto found = std::ranges::find_if(value.completion.results, [&](const auto& item) {
      return item.source.uri == failure.source.uri;
    });
    if (found != value.completion.results.end()) {
      *found = failure;
    }
    value.summary = failure.result;
  }
  if (value.cpu->documents) {
    const auto retained = value.cpu->documents->retain_verified(value.cpu->batch);
    if (retained != GNEISS_SUCCESS) {
      finish(retained, texture_load_state::failed);
      return false;
    }
    value.cpu->documents.reset();
  }
  value.prepared = true;
  if (notification_) {
    notification_->notify();
  }
  value.commit_started = std::chrono::steady_clock::now();
  return true;
}
void texture_load_service::advance_impl() {
  if (!pending_) {
    return;
  }
  auto& value = *pending_;
  if (!value.prepared && !advance_preparation(value)) {
    return;
  }
  if (!value.uploading) {
    if (value.cancelled) {
      finish(GNEISS_ERROR_INVALID_STATE, texture_load_state::cancelled);
      return;
    }

    const auto start = std::chrono::steady_clock::now();
    for (unsigned count = 0U; value.staged_count < value.cpu->batch.assets.size() && count < 4U;
         ++count) {
      render_asset_loader::asset_candidate candidate;
      const auto source = value.cpu->batch.assets[value.staged_count].source;
      if (std::ranges::none_of(value.completion.results,
                               [&](const auto& item) { return item.source.uri == source.uri; })) {
        value.completion.results.push_back({.source = source});
      }
      const auto target = std::ranges::find_if(value.targets, [&](const auto& lease) {
        return lease.uri() == source.uri && lease.type() == source.type;
      });
      const auto result = engine::core::diagnostics::measure(
          engine::core::diagnostics::loop_stage::asset_stage, [&] {
            return loader_.stage_asset(
                std::move(value.cpu->batch.assets[value.staged_count]), value.candidates, candidate,
                target == value.targets.end() ? render_asset_lease{} : *target,
                value.update_sequence);
          });
      if (result != GNEISS_SUCCESS) {
        value.cpu->message = "资源上传数据准备失败";
        value.summary = result;
        auto item = std::ranges::find_if(value.completion.results, [&](const auto& entry) {
          return entry.source.uri == source.uri;
        });
        item->result = result;
        ++value.staged_count;
        continue;
      }
      {
        render_upload_item upload{
            .mesh = candidate.mesh,
            .material = candidate.material,
            .texture = candidate.texture,
            .dependency_textures = candidate.dependency_textures,
            .bytes = candidate.bytes,
            .texture_payload = candidate.texture_payload,
        };
        if (backend_.estimate_bytes) {
          upload.bytes = backend_.estimate_bytes(upload);
        }
        value.data.push_back(std::move(upload));
      }
      value.candidates.push_back(std::move(candidate));
      ++value.staged_count;
      if (notification_) {
        notification_->notify();
      }
      if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(2)) {
        break;
      }
    }
    if (value.staged_count != value.cpu->batch.assets.size()) {
      return;
    }
  }
  if (value.in_flight) {
    gneiss_result result{};
    if (!engine::core::diagnostics::measure(engine::core::diagnostics::loop_stage::upload_poll,
                                            [&] { return backend_.poll(value.upload, result); })) {
      return;
    }
    value.in_flight = false;
    value.upload_reserved_bytes = 0U;
    if (value.discarding) {
      if (!value.discard_item) {
        finish(value.failure,
               value.cancelled ? texture_load_state::cancelled : texture_load_state::failed);
        return;
      }
      value.discard_item = false;
      value.discarding = false;
      value.processed_uploads = value.next_upload;
    } else {
      auto& candidate = value.candidates[value.processed_uploads];
      auto item = std::ranges::find_if(value.completion.results, [&](const auto& entry) {
        return entry.source.uri == candidate.source.uri;
      });
      const bool uploaded = result == GNEISS_SUCCESS;
      if (uploaded && !value.cancelled) {
        result = loader_.publish_assets(
            std::span(value.candidates).subspan(value.processed_uploads, 1U));
      }
      if (!value.cancelled && result == GNEISS_SUCCESS) {
        item->result = GNEISS_SUCCESS;
        item->applied = true;
        value.processed_uploads = value.next_upload;
        ++value.completed_uploads;
      } else if (!value.cancelled) {
        item->result = result;
        value.summary = result;
        value.discard_item = uploaded;
        if (!uploaded) {
          value.processed_uploads = value.next_upload;
        }
      }
      if (backend_.elapsed_ms) {
        value.completion.upload_ms += backend_.elapsed_ms();
      }
    }
  }
  if (value.discard_item) {
    texture_upload_backend::data failed{value.data[value.processed_uploads]};
    const auto discarded = backend_.discard(std::move(failed), value.upload);
    if (discarded == GNEISS_ERROR_NOT_READY) {
      return;
    }
    if (discarded != GNEISS_SUCCESS) {
      value.failure = discarded;
      value.discard_item = false;
    } else {
      value.discarding = true;
      value.in_flight = true;
      return;
    }
  }
  if (value.cancelled) {
    value.failure = GNEISS_ERROR_INVALID_STATE;
  }
  if (value.failure != GNEISS_SUCCESS) {
    if (value.processed_uploads == value.data.size()) {
      finish(value.failure,
             value.cancelled ? texture_load_state::cancelled : texture_load_state::failed);
      return;
    }
    const auto remaining = std::span(value.data).subspan(value.processed_uploads);
    texture_upload_backend::data unpublished(remaining.begin(), remaining.end());
    const auto result = backend_.discard(std::move(unpublished), value.upload);
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
    const auto index = value.next_upload;
    const auto& upload = value.data[index];
    if (upload.bytes > maximum_upload_bytes) {
      value.cpu->message = "GPU 单资源上传预算不足";
      auto item = std::ranges::find_if(value.completion.results, [&](const auto& entry) {
        return entry.source.uri == value.candidates[index].source.uri;
      });
      item->result = GNEISS_ERROR_OUT_OF_MEMORY;
      value.summary = item->result;
      value.processed_uploads = ++value.next_upload;
      return;
    }
    texture_upload_backend::data batch{upload};
    const auto bytes = upload.bytes;
    const auto result = engine::core::diagnostics::measure(
        engine::core::diagnostics::loop_stage::upload_submit,
        [&] { return backend_.begin(std::move(batch), value.upload); });
    if (result == GNEISS_ERROR_NOT_READY) {
      return;
    }
    if (result != GNEISS_SUCCESS) {
      value.cpu->message = "资产上传提交失败";
      auto item = std::ranges::find_if(value.completion.results, [&](const auto& entry) {
        return entry.source.uri == value.candidates[value.next_upload].source.uri;
      });
      item->result = result;
      value.summary = result;
      value.processed_uploads = ++value.next_upload;
      return;
    }
    value.upload_reserved_bytes = bytes;
    value.completion.peak_upload_bytes = std::max(value.completion.peak_upload_bytes, bytes);
    value.uploading = true;
    value.in_flight = true;
    ++value.next_upload;
    return;
  }
  value.completion.commit_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - value.commit_started)
                                   .count();
  finish(value.summary, value.summary == GNEISS_SUCCESS ? texture_load_state::applied
                                                        : texture_load_state::failed);
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
bool texture_load_service::cancel_unpublished() {
  check_owner();
  if (!pending_) {
    return false;
  }
  pending_->cancelled = true;
  (void)executor_.cancel(pending_->task);
  return true;
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
} // namespace gneiss::engine::render
