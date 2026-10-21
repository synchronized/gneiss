// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/asset_request.hpp"
#include <type_traits>

struct sample_asset final : caors::resources::resource {
  int value = 42;
};
using request = gneiss::engine::resource::asset_request<sample_asset>;
using native_request = caors::resources::load_request<sample_asset>;
static_assert(!std::is_copy_constructible_v<request>);
static_assert(std::is_nothrow_move_constructible_v<request>);
static_assert(std::is_nothrow_move_assignable_v<request>);
// 包装不增加第二份终态或虚表。
static_assert(sizeof(request) == sizeof(native_request));
int main() {
  using gneiss::result;
  auto asset = caors::resources::make_handle<sample_asset>(nullptr);
  auto output = asset;
  request empty;
  if (empty.take(output) != result::invalid_state || output.get() != asset.get())
    return 1;
  request failed{native_request::failed({caors::error_domain::resources, caors::errc::not_found})};
  if (failed.take(output) != result::not_found || failed.take(output) != result::not_found ||
      output.get() != asset.get())
    return 2;
  request ready{native_request::completed(asset)};
  request moved{std::move(ready)};
  if (ready || !moved || ready.take(output) != result::invalid_state)
    return 3;
  output = {};
  if (moved.take(output) != result::success || output.get() != asset.get() || output->value != 42 ||
      moved.take(output) != result::invalid_state || output.get() != asset.get())
    return 4;
  asset = {};
  if (!output || output->value != 42)
    return 5;
  request cancelled{native_request::completed(output)};
  cancelled.cancel();
  if (cancelled || cancelled.take(output) != result::invalid_state || !output)
    return 6;
  return 0;
}
