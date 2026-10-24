// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_asset_reloader.hpp"

#include <thread>

int main() {
  using namespace gneiss;
  using namespace gneiss::host::runtime;
  unsigned calls = 0U;
  gneiss::engine::core::result next_result = gneiss::engine::core::result::success;
  runtime_asset_reloader reloader([&](std::span<const ipc_asset_revision> assets) {
    ++calls;
    return assets.empty() ? gneiss::engine::core::result::invalid_argument : next_result;
  });
  ipc_asset_reload_request request{
      .session_id = 3U,
      .revision = 1U,
      .assets = {{.uri = "asset://main.scene", .type = ipc_asset_type::scene}}};
  ipc_asset_reload_result response;
  if (reloader.execute(request, response) != gneiss::engine::core::result::success ||
      response.status != ipc_asset_apply_status::applied || calls != 1U ||
      reloader.applied_revision() != 1U) {
    return 1;
  }
  if (reloader.execute(request, response) != gneiss::engine::core::result::success ||
      response.status != ipc_asset_apply_status::stale || calls != 1U) {
    return 2;
  }
  request.revision = 2U;
  next_result = gneiss::engine::core::result::io;
  if (reloader.execute(request, response) != gneiss::engine::core::result::success ||
      response.status != ipc_asset_apply_status::failed || calls != 2U ||
      reloader.applied_revision() != 1U) {
    return 3;
  }
  next_result = gneiss::engine::core::result::unsupported;
  if (reloader.execute(request, response) != gneiss::engine::core::result::success ||
      response.status != ipc_asset_apply_status::restart_required || calls != 3U ||
      reloader.applied_revision() != 1U) {
    return 4;
  }
  next_result = gneiss::engine::core::result::success;
  request.session_id = 4U;
  if (reloader.execute(request, response) != gneiss::engine::core::result::success ||
      response.status != ipc_asset_apply_status::applied || reloader.session_id() != 4U ||
      reloader.applied_revision() != 2U) {
    return 5;
  }
  gneiss::engine::core::result thread_result = gneiss::engine::core::result::success;
  std::thread other([&] { thread_result = reloader.execute(request, response); });
  other.join();
  if (thread_result != gneiss::engine::core::result::invalid_state) {
    return 6;
  }
  bool completed{};
  unsigned accepted{};
  gneiss::engine::core::result async_result = gneiss::engine::core::result::success;
  gneiss::engine::core::result begin_result = gneiss::engine::core::result::success;
  runtime_asset_reloader asynchronous(
      [&](auto) {
        ++calls;
        return gneiss::engine::core::result::success;
      },
      [&](const auto&) {
        ++accepted;
        return begin_result;
      },
      [&](gneiss::engine::core::result& output, std::vector<ipc_asset_item_result>& items,
          bool& ready) {
        ready = completed;
        output = async_result;
        if (completed) {
          items = {{{"asset://a.texture.json", ipc_asset_type::texture},
                    gneiss::engine::core::result::success,
                    true}};
        }
        return gneiss::engine::core::result::success;
      });
  request = {.session_id = 10U,
             .revision = 1U,
             .assets = {{.uri = "asset://a.texture.json", .type = ipc_asset_type::texture}}};
  if (asynchronous.execute(request, response) != gneiss::engine::core::result::not_ready ||
      accepted != 1U || asynchronous.applied_revision() != 0U) {
    return 7;
  }
  if (asynchronous.execute(request, response) != gneiss::engine::core::result::invalid_state ||
      accepted != 1U) {
    return 13;
  }
  bool ready = true;
  if (asynchronous.advance(response, ready) != gneiss::engine::core::result::success || ready ||
      asynchronous.applied_revision() != 0U) {
    return 8;
  }
  completed = true;
  if (asynchronous.advance(response, ready) != gneiss::engine::core::result::success || !ready ||
      response.status != ipc_asset_apply_status::applied || response.revision != 1U ||
      asynchronous.applied_revision() != 1U) {
    return 9;
  }
  if (asynchronous.advance(response, ready) != gneiss::engine::core::result::success || ready) {
    return 10;
  }
  request.revision = 2U;
  request.assets.front().uri = "asset://b.texture.json";
  request.assets.push_back({"asset://m.material.json", ipc_asset_type::material});
  request.assets.push_back({"asset://g.mesh.json", ipc_asset_type::static_mesh});
  if (asynchronous.execute(request, response) != gneiss::engine::core::result::not_ready ||
      accepted != 2U) {
    return 11;
  }
  async_result = gneiss::engine::core::result::io;
  if (asynchronous.advance(response, ready) != gneiss::engine::core::result::success || !ready ||
      response.status != ipc_asset_apply_status::failed || response.completed_assets != 1U ||
      response.items.size() != 1U || !response.items.front().applied ||
      asynchronous.applied_revision() != 1U) {
    return 12;
  }
  const auto synchronous_calls = calls;
  begin_result = gneiss::engine::core::result::io;
  if (asynchronous.execute(request, response) != gneiss::engine::core::result::success ||
      response.status != ipc_asset_apply_status::failed || calls != synchronous_calls ||
      asynchronous.applied_revision() != 1U) {
    return 14;
  }
  begin_result = gneiss::engine::core::result::success;
  async_result = gneiss::engine::core::result::success;
  if (asynchronous.execute(request, response) != gneiss::engine::core::result::not_ready ||
      asynchronous.advance(response, ready) != gneiss::engine::core::result::success || !ready ||
      response.status != ipc_asset_apply_status::applied || calls != synchronous_calls) {
    return 15;
  }
  request.revision = 3U;
  request.assets.push_back({.uri = "asset://main.scene", .type = ipc_asset_type::scene});
  const auto previous_accepted = accepted;
  if (asynchronous.execute(request, response) != gneiss::engine::core::result::invalid_argument ||
      response.status != ipc_asset_apply_status::failed || accepted != previous_accepted ||
      calls != synchronous_calls || asynchronous.applied_revision() != 2U) {
    return 16;
  }
  // 未装配或仅装配一个异步回调时，三类渲染资产都不能进入同步 apply。
  for (unsigned wiring = 0U; wiring < 3U; ++wiring) {
    runtime_asset_reloader::begin_function begin;
    runtime_asset_reloader::poll_function poll;
    if (wiring == 1U) {
      begin = [&](const auto&) {
        ++accepted;
        return gneiss::engine::core::result::success;
      };
    }
    if (wiring == 2U) {
      poll = [&](gneiss::engine::core::result&, std::vector<ipc_asset_item_result>&, bool&) {
        ++accepted;
        return gneiss::engine::core::result::success;
      };
    }
    runtime_asset_reloader incomplete(
        [&](auto) {
          ++calls;
          return gneiss::engine::core::result::success;
        },
        begin, poll);
    for (const auto type :
         {ipc_asset_type::texture, ipc_asset_type::material, ipc_asset_type::static_mesh}) {
      request.assets = {{.uri = "asset://asset.json", .type = type}};
      if (incomplete.execute(request, response) != gneiss::engine::core::result::success ||
          response.status != ipc_asset_apply_status::failed || calls != synchronous_calls ||
          accepted != previous_accepted || incomplete.applied_revision() != 0U) {
        return 17;
      }
      if (incomplete.advance(response, ready) != gneiss::engine::core::result::success || ready) {
        return 18;
      }
    }
  }
  return 0;
}
