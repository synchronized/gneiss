// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/asset_render_state.hpp"
namespace gneiss::engine {
result asset_manager::attach_render(render_service& render, texture_prepare_profile profile) {
  if (render_stopping_)
    return result::invalid_state;
  if (render_state_)
    return &render_state_->factory.render() == &render ? result::success : result::invalid_state;
  render_state_ = std::make_unique<render_state>(*this, render);
  render_state_->loader.set_default_profile(profile);
  return result::success;
}
result asset_manager::configure_loading(task_executor& executor, texture_prepare_profile profile,
                                        std::shared_ptr<progress_notification> notification) {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  attach_task_executor(executor);
  if (render_state_->batch)
    return result::success;
  render_state_->loader.set_default_profile(profile);
  render_state_->batch =
      std::make_unique<texture_load_service>(executor, *this, profile, std::move(notification));
  return result::success;
}
void asset_manager::set_default_profile(texture_prepare_profile profile) noexcept {
  if (render_state_)
    render_state_->loader.set_default_profile(profile);
}
void asset_manager::advance_render() {
  if (!render_state_)
    return;
  (void)render_state_->factory.drain();
  if (render_state_->batch)
    render_state_->batch->advance();
}
void asset_manager::stop_render() noexcept {
  render_stopping_ = true;
  if (render_state_)
    render_state_->batch.reset();
}
void asset_manager::advance() { advance_resources(); }
void asset_manager::request_stop() {
  render_stopping_ = true;
  if (render_state_ && render_state_->batch)
    render_state_->batch->request_stop();
}
bool asset_manager::stopped() const {
  return !render_state_ || !render_state_->batch || render_state_->batch->stopped();
}
void asset_manager::release_unused() noexcept {
  if (render_state_)
    render_state_->loader.release_unused();
  else
    cache_.release_unused();
}
result asset_manager::reserve_mesh(mesh& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.reserve_mesh(out);
}
mesh asset_manager::find_mesh(rid id) const noexcept {
  return render_state_ ? render_state_->factory.find_mesh(id) : mesh{};
}
bool asset_manager::owns(const mesh& value) const noexcept {
  return render_state_ && render_state_->loader.owns(value);
}
result asset_manager::acquire_mesh(std::string_view uri, mesh& out,
                                   asset_diagnostic& diagnostic) noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  return render_state_->loader.acquire_mesh(uri, out, diagnostic);
}
result asset_manager::reserve_texture(texture& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.reserve_texture(out);
}
texture asset_manager::find_texture(rid id) const noexcept {
  return render_state_ ? render_state_->factory.find_texture(id) : texture{};
}
bool asset_manager::owns(const texture& value) const noexcept {
  return render_state_ && render_state_->loader.owns(value);
}
result asset_manager::acquire_texture(std::string_view uri, texture& out,
                                      asset_diagnostic& diagnostic) noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  return render_state_->loader.acquire_texture(uri, out, diagnostic);
}
result asset_manager::reserve_material(material& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.reserve_material(out);
}
material asset_manager::find_material(rid id) const noexcept {
  return render_state_ ? render_state_->factory.find_material(id) : material{};
}
bool asset_manager::owns(const material& value) const noexcept {
  return render_state_ && render_state_->loader.owns(value);
}
result asset_manager::acquire_material(std::string_view uri, material& out,
                                       asset_diagnostic& diagnostic) noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  return render_state_->loader.acquire_material(uri, out, diagnostic);
}
result asset_manager::create_mesh(const mesh_view& data, mesh& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.create_mesh(data, out);
}
result asset_manager::create_texture(const texture_view& data, texture& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.create_texture(data, out);
}
result asset_manager::create_material(const material_parameters& data, std::array<texture, 5> deps,
                                      material& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.create_material(data, std::move(deps), out);
}
result asset_manager::reserve_asset(const render_asset_reload& source,
                                    render_asset_ref& out) noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  return render_state_->loader.reserve_asset(source, out);
}
result asset_manager::acquire_cached(const render_asset_reload& source, render_asset_ref& out,
                                     texture_prepare_profile profile) const noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  return render_state_->loader.acquire_cached(source, out, profile);
}
result asset_manager::acquire_asset(render_asset_type type, std::string_view uri,
                                    render_asset_ref& out, asset_diagnostic& diagnostic) noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  return render_state_->loader.acquire_asset(type, uri, out, diagnostic);
}
bool asset_manager::owns(const render_asset_ref& value) const noexcept {
  return render_state_ && render_state_->loader.owns(value);
}
result asset_manager::submit_assets(std::span<const render_asset_reload> sources,
                                    std::uint64_t session, std::uint64_t revision,
                                    std::uint64_t& request, bool reload, std::size_t limit) {
  if (!render_state_ || !render_state_->batch || render_stopping_) {
    request = 0U;
    return result::invalid_state;
  }
  return render_state_->batch->submit_assets(sources, session, revision, request, reload, limit);
}
result asset_manager::submit(std::span<const std::string> uris, std::uint64_t session,
                             std::uint64_t revision, std::uint64_t& request, bool reload) {
  if (!render_state_ || !render_state_->batch || render_stopping_) {
    request = 0U;
    return result::invalid_state;
  }
  return render_state_->batch->submit(uris, session, revision, request, reload);
}
bool asset_manager::progress(asset_load_progress& out) const {
  return render_state_ && render_state_->batch && render_state_->batch->progress(out);
}
bool asset_manager::take(texture_load_completion& out) {
  return render_state_ && render_state_->batch && render_state_->batch->take(out);
}
bool asset_manager::busy() const noexcept {
  return render_state_ && render_state_->batch && render_state_->batch->busy();
}
bool asset_manager::cancel() {
  return render_state_ && render_state_->batch && render_state_->batch->cancel();
}
bool asset_manager::cancel_unpublished() {
  return render_state_ && render_state_->batch && render_state_->batch->cancel_unpublished();
}
texture_prepare_profile asset_manager::profile() const noexcept {
  return render_state_ && render_state_->batch ? render_state_->batch->profile()
                                               : texture_prepare_profile{};
}
std::span<const render_asset_ref> asset_manager::requested_assets() const {
  return render_state_ && render_state_->batch ? render_state_->batch->requested_assets()
                                               : std::span<const render_asset_ref>{};
}
std::uint64_t asset_manager::next_update_sequence() noexcept {
  return render_state_ && !render_stopping_ ? render_state_->loader.next_update_sequence() : 0U;
}
bool asset_manager::claim_update(const render_asset_ref& target, std::uint64_t sequence) noexcept {
  return render_state_ && !render_stopping_ && render_state_->loader.claim_update(target, sequence);
}
result asset_manager::prepare_update(prepared_render_asset data, asset_update& out,
                                     const render_asset_ref& target,
                                     std::uint64_t sequence) noexcept {
  return render_state_ && !render_stopping_
             ? render_state_->loader.prepare_update(std::move(data), out, target, sequence)
             : result::invalid_state;
}
result asset_manager::apply_update(asset_update& update) noexcept {
  return render_state_ && !render_stopping_ ? render_state_->loader.apply_update(update)
                                            : result::invalid_state;
}
}
