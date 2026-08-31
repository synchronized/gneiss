// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_GAME_MODULE_H_
#define GNEISS_GAME_MODULE_H_

#include <stddef.h>
#include <stdint.h>

#include <gneiss/core/export.h>
#include <gneiss/core/result.h>

/** Game Module ABI 的首个 Experimental 版本。 */
#define GNEISS_GAME_MODULE_ABI_VERSION_1 UINT32_C(1)
#define GNEISS_GAME_MODULE_ABI_VERSION_CURRENT GNEISS_GAME_MODULE_ABI_VERSION_1

/** Runtime 通过动态库查找的固定 UTF-8 符号名。 */
#define GNEISS_GAME_MODULE_QUERY_SYMBOL "gneiss_game_module_query"

/** Engine 持有的 Game Context 句柄；零值无效，不得持久化。 */
typedef uint64_t gneiss_game_context;
#define GNEISS_NULL_GAME_CONTEXT UINT64_C(0)

/** 单次游戏逻辑更新的时间信息；所有时间均来自单调时钟。 */
typedef struct gneiss_game_update_time {
  uint32_t struct_size;
  uint32_t reserved;
  uint64_t update_index;
  uint64_t delta_ns;
  uint64_t elapsed_ns;
} gneiss_game_update_time;

#define GNEISS_GAME_UPDATE_TIME_VERSION_1_SIZE                                                     \
  ((uint32_t)(offsetof(gneiss_game_update_time, elapsed_ns) + sizeof(uint64_t)))

/** 初始化模块私有状态。成功时必须写入 `out_module_state`。 */
typedef gneiss_result (*gneiss_game_module_initialize_fn)(gneiss_game_context context,
                                                          void** out_module_state);
/** 在 Runtime 主线程执行一次固定步长更新。 */
typedef gneiss_result (*gneiss_game_module_fixed_update_fn)(gneiss_game_context context,
                                                            void* module_state,
                                                            const gneiss_game_update_time* time);
/** 在 Runtime 主线程执行一次逐帧更新。 */
typedef gneiss_result (*gneiss_game_module_update_fn)(gneiss_game_context context,
                                                      void* module_state,
                                                      const gneiss_game_update_time* time);
/** 关闭模块并销毁私有状态；仅在初始化成功后调用，且最多调用一次。 */
typedef gneiss_result (*gneiss_game_module_shutdown_fn)(gneiss_game_context context,
                                                        void* module_state);

/** Game Module 查询入口返回的版本化描述。字符串必须在动态库卸载前保持有效。 */
typedef struct gneiss_game_module_desc {
  uint32_t struct_size;
  uint32_t abi_version;
  const char* module_id;
  uint64_t module_id_length;
  gneiss_game_module_initialize_fn initialize;
  gneiss_game_module_fixed_update_fn fixed_update;
  gneiss_game_module_update_fn update;
  gneiss_game_module_shutdown_fn shutdown;
  uint64_t reserved[2];
} gneiss_game_module_desc;

#define GNEISS_GAME_MODULE_DESC_VERSION_1_SIZE                                                     \
  ((uint32_t)(offsetof(gneiss_game_module_desc, reserved) + sizeof(uint64_t) * 2U))

/** Runtime 查找并调用的 Game Module 查询入口类型。 */
typedef gneiss_result (*gneiss_game_module_query_fn)(uint32_t engine_abi_version,
                                                     gneiss_game_module_desc* out_desc);

#if defined(GNEISS_BUILDING_GAME_MODULE)
#if defined(_WIN32) || defined(__CYGWIN__)
#define GNEISS_GAME_MODULE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define GNEISS_GAME_MODULE_EXPORT __attribute__((visibility("default")))
#else
#define GNEISS_GAME_MODULE_EXPORT
#endif
#else
#define GNEISS_GAME_MODULE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Game Module 必须实现的固定查询入口。
 *
 * 调用方预先填写 `out_desc->struct_size`。实现只能写入该大小覆盖的字段，并应拒绝不支持的 Engine
 * ABI 版本。该函数同步调用，不得保存 `out_desc`。
 */
GNEISS_GAME_MODULE_EXPORT gneiss_result gneiss_game_module_query(uint32_t engine_abi_version,
                                                                 gneiss_game_module_desc* out_desc);

/**
 * 校验首版 Game Module 描述。
 *
 * 不取得描述、字符串或回调的所有权。线程安全。
 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result
gneiss_game_module_validate(const gneiss_game_module_desc* desc);

#ifdef __cplusplus
}
#endif

#endif
