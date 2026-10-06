// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/game_module.hpp>

#include <array>
#include <cstdint>
#include <type_traits>

namespace {
struct module_state final {
  std::uint64_t fixed_ticks = 0;
  std::uint64_t ticks = 0;
  bool stopped = false;
};
module_state test_state;

gneiss::result initialize(gneiss::game_context context, void*& output) noexcept {
  output = &test_state;
  return context.is_valid() ? gneiss::result::success : gneiss::result::invalid_handle;
}
gneiss::result fixed_update(gneiss::game_context context, void* value,
                            const gneiss::game_update_time& time) noexcept {
  if (value != &test_state || context.get() != 42 || time.update_index != 7 ||
      time.delta_ns != 16 || time.elapsed_ns != 112) {
    return gneiss::result::invalid_argument;
  }
  ++test_state.fixed_ticks;
  return gneiss::result::success;
}
gneiss::result update(gneiss::game_context context, void* value,
                      const gneiss::game_update_time& time) noexcept {
  const auto status = fixed_update(context, value, time);
  if (status.ok()) {
    ++test_state.ticks;
  }
  return status;
}
gneiss::result shutdown(gneiss::game_context /*context*/, void* value) noexcept {
  if (value != &test_state) {
    return gneiss::result::invalid_argument;
  }
  test_state.stopped = true;
  return gneiss::result::success;
}
constexpr gneiss::game_module_callbacks callbacks{
    .initialize = initialize,
    .fixed_update = fixed_update,
    .update = update,
    .shutdown = shutdown,
};
using module = gneiss::game_module<callbacks>;

bool check_query() {
  static_assert(gneiss::game_module_abi_version == gneiss::game_module_abi_version_1);
  static_assert(gneiss::game_module_query_symbol == "gneiss_game_module_query");
  static_assert(std::is_trivially_copyable_v<gneiss::game_context>);
  constexpr gneiss::game_module_desc description{.module_id = "test.native"};
  if (module::validate(description).failed() ||
      module::validate({}) != gneiss::result::invalid_argument ||
      gneiss::game_module<gneiss::game_module_callbacks{}>::validate(description) !=
          gneiss::result::invalid_argument) {
    return false;
  }
  struct extended_desc final {
    gneiss_game_module_desc value{};
    std::array<std::uint64_t, 2> tail{123, 456};
  } output;
  output.value.struct_size = sizeof(output);
  output.value.module_id_length = 99;
  if (module::export_query(99, &output.value, description) != gneiss::result::unsupported ||
      output.value.module_id_length != 99 ||
      module::export_query(gneiss::game_module_abi_version, nullptr, description) !=
          gneiss::result::invalid_argument ||
      module::export_query(gneiss::game_module_abi_version, &output.value, {}) !=
          gneiss::result::invalid_argument ||
      output.value.module_id_length != 99) {
    return false;
  }
  output.value.struct_size = 4;
  if (module::export_query(gneiss::game_module_abi_version, &output.value, description) !=
          gneiss::result::unsupported ||
      output.value.module_id_length != 99) {
    return false;
  }
  output.value.struct_size = sizeof(output);
  return module::export_query(gneiss::game_module_abi_version, &output.value, description).ok() &&
         output.value.struct_size == sizeof(output) && output.value.module_id_length == 11 &&
         output.tail[0] == 123 && output.tail[1] == 456 &&
         gneiss::validate_game_module_native(output.value).ok();
}

bool check_lifecycle() {
  const auto native = module::to_native({.module_id = "test.native"});
  int sentinel = 0;
  void* output = &sentinel;
  if (native.initialize(42, nullptr) != GNEISS_ERROR_INVALID_ARGUMENT ||
      native.initialize(0, &output) != GNEISS_ERROR_INVALID_HANDLE || output != &sentinel ||
      native.initialize(42, &output) != GNEISS_SUCCESS || output != &test_state) {
    return false;
  }
  gneiss_game_update_time time{
      .struct_size = sizeof(gneiss_game_update_time),
      .reserved = 0,
      .update_index = 7,
      .delta_ns = 16,
      .elapsed_ns = 112,
  };
  if (native.fixed_update(42, output, &time) != GNEISS_SUCCESS ||
      native.update(42, output, &time) != GNEISS_SUCCESS || test_state.fixed_ticks != 2 ||
      test_state.ticks != 1 ||
      native.update(42, output, nullptr) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return false;
  }
  time.reserved = 1;
  if (native.update(42, output, &time) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return false;
  }
  time.reserved = 0;
  time.struct_size = 0;
  return native.fixed_update(42, output, &time) == GNEISS_ERROR_INVALID_ARGUMENT &&
         test_state.fixed_ticks == 2 && test_state.ticks == 1 &&
         native.shutdown(42, output) == GNEISS_SUCCESS && test_state.stopped;
}
} // namespace

int main() {
  const gneiss::game_context context{42U};
  const auto message = gneiss::make_log_message(gneiss::log_severity::info, "test", "message");
  if (!context.is_valid() || gneiss::null_game_context.is_valid() ||
      context.log(message) != gneiss::result::invalid_handle) {
    return 1;
  }
  return check_query() && check_lifecycle() ? 0 : 2;
}
