// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_asset_reloader.h"

#include <thread>

int main() {
  using namespace gneiss;
  using namespace gneiss::runtime_internal;
  unsigned calls = 0U;
  result next_result = result::success;
  runtime_asset_reloader reloader([&](std::span<const ipc_asset_revision> assets) {
    ++calls;
    return assets.empty() ? result::invalid_argument : next_result;
  });
  ipc_asset_reload_request request{.session_id = 3U,
                                   .revision = 1U,
                                   .assets = {{.uri = "asset://imported/a/mesh-0.gneiss-mesh",
                                               .type = ipc_asset_type::static_mesh}}};
  ipc_asset_reload_result response;
  if (reloader.execute(request, response) != result::success ||
      response.status != ipc_asset_apply_status::applied || calls != 1U ||
      reloader.applied_revision() != 1U) {
    return 1;
  }
  if (reloader.execute(request, response) != result::success ||
      response.status != ipc_asset_apply_status::stale || calls != 1U) {
    return 2;
  }
  request.revision = 2U;
  next_result = result::io;
  if (reloader.execute(request, response) != result::success ||
      response.status != ipc_asset_apply_status::failed || calls != 2U ||
      reloader.applied_revision() != 1U) {
    return 3;
  }
  next_result = result::unsupported;
  if (reloader.execute(request, response) != result::success ||
      response.status != ipc_asset_apply_status::restart_required || calls != 3U ||
      reloader.applied_revision() != 1U) {
    return 4;
  }
  next_result = result::success;
  request.session_id = 4U;
  if (reloader.execute(request, response) != result::success ||
      response.status != ipc_asset_apply_status::applied || reloader.session_id() != 4U ||
      reloader.applied_revision() != 2U) {
    return 5;
  }
  result thread_result = result::success;
  std::thread other([&] { thread_result = reloader.execute(request, response); });
  other.join();
  if (thread_result != result::invalid_state) {
    return 6;
  }
  bool completed{};
  unsigned accepted{};
  result async_result = result::success;
  runtime_asset_reloader asynchronous(
      [&](auto) {
        ++calls;
        return result::success;
      },
      [&](const auto&) {
        ++accepted;
        return result::success;
      },
      [&](result& output, bool& ready) {
        ready = completed;
        output = async_result;
        return result::success;
      });
  request = {.session_id = 10U,
             .revision = 1U,
             .assets = {{.uri = "asset://a.texture.json", .type = ipc_asset_type::texture}}};
  if (asynchronous.execute(request, response) != result::not_ready || accepted != 1U ||
      asynchronous.applied_revision() != 0U) {
    return 7;
  }
  if (asynchronous.execute(request, response) != result::invalid_state || accepted != 1U) {
    return 13;
  }
  bool ready = true;
  if (asynchronous.advance(response, ready) != result::success || ready ||
      asynchronous.applied_revision() != 0U) {
    return 8;
  }
  completed = true;
  if (asynchronous.advance(response, ready) != result::success || !ready ||
      response.status != ipc_asset_apply_status::applied || response.revision != 1U ||
      asynchronous.applied_revision() != 1U) {
    return 9;
  }
  if (asynchronous.advance(response, ready) != result::success || ready) {
    return 10;
  }
  request.revision = 2U;
  request.assets.front().uri = "asset://b.texture.json";
  request.assets.push_back({"asset://m.material.json", ipc_asset_type::material});
  request.assets.push_back({"asset://g.mesh.json", ipc_asset_type::static_mesh});
  if (asynchronous.execute(request, response) != result::not_ready || accepted != 2U) {
    return 11;
  }
  async_result = result::io;
  if (asynchronous.advance(response, ready) != result::success || !ready ||
      response.status != ipc_asset_apply_status::failed || asynchronous.applied_revision() != 1U) {
    return 12;
  }
  return 0;
}
