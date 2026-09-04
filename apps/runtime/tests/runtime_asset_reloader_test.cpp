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
  return thread_result == result::invalid_state ? 0 : 6;
}
