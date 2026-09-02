// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/game_module.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

#if !defined(GNEISS_TEST_MISSING_QUERY)
#if !defined(GNEISS_TEST_INITIALIZE_FAILURE)
int module_state{};
#endif

void trace(const char event) {
#if defined(_WIN32)
  std::size_t length{};
  (void)getenv_s(&length, nullptr, 0, "GNEISS_GAME_MODULE_TEST_TRACE");
  if (length == 0) {
    return;
  }
  std::string path(length, '\0');
  (void)getenv_s(&length, path.data(), path.size(), "GNEISS_GAME_MODULE_TEST_TRACE");
  FILE* file{};
  (void)fopen_s(&file, path.c_str(), "ab");
#else
  const char* path = std::getenv("GNEISS_GAME_MODULE_TEST_TRACE");
  if (path == nullptr) {
    return;
  }
  FILE* file = std::fopen(path, "ab");
#endif
  if (file != nullptr) {
    (void)std::fputc(event, file);
    (void)std::fclose(file);
  }
}

gneiss_result initialize(gneiss_game_context, void** out_state) {
  trace('I');
#if defined(GNEISS_TEST_INITIALIZE_FAILURE)
  (void)out_state;
  return GNEISS_ERROR_INITIALIZATION_FAILED;
#else
  *out_state = &module_state;
  return GNEISS_SUCCESS;
#endif
}

gneiss_result fixed_update(gneiss_game_context, void*, const gneiss_game_update_time*) {
  trace('F');
  return GNEISS_SUCCESS;
}

gneiss_result update(gneiss_game_context, void*, const gneiss_game_update_time* time) {
  trace('U');
  return time->update_index == UINT64_C(99) ? GNEISS_ERROR_INTERNAL : GNEISS_SUCCESS;
}

gneiss_result shutdown(gneiss_game_context, void*) {
  trace('S');
  return GNEISS_SUCCESS;
}
#endif

} // namespace

#if defined(GNEISS_TEST_MISSING_QUERY)
extern "C" GNEISS_GAME_MODULE_EXPORT void gneiss_test_unrelated_symbol(void) {}
#else
extern "C" GNEISS_GAME_MODULE_EXPORT gneiss_result
gneiss_game_module_query(uint32_t engine_abi_version, gneiss_game_module_desc* out_desc) {
  if (engine_abi_version != GNEISS_GAME_MODULE_ABI_VERSION_CURRENT || out_desc == nullptr ||
      out_desc->struct_size < GNEISS_GAME_MODULE_DESC_VERSION_1_SIZE) {
    return GNEISS_ERROR_UNSUPPORTED;
  }
  const uint32_t size = out_desc->struct_size;
  std::memset(out_desc, 0, size);
  out_desc->struct_size = size;
#if defined(GNEISS_TEST_INVALID_ABI)
  out_desc->abi_version = UINT32_C(999);
#else
  out_desc->abi_version = GNEISS_GAME_MODULE_ABI_VERSION_CURRENT;
#endif
  out_desc->module_id = "gneiss.test.fixture";
  out_desc->module_id_length = sizeof("gneiss.test.fixture") - 1U;
  out_desc->initialize = initialize;
  out_desc->fixed_update = fixed_update;
  out_desc->update = update;
  out_desc->shutdown = shutdown;
  return GNEISS_SUCCESS;
}
#endif
