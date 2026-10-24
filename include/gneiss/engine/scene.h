// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_H_
#define GNEISS_SCENE_H_

#include <gneiss/engine/game_module_types.h>
#include <stddef.h>
#include <stdint.h>

#include <gneiss/engine/core/entity.h>
#include <gneiss/engine/core/export.h>
#include <gneiss/engine/core/result.h>
#include <gneiss/engine/world.h>

/** Scene Node 的运行时标识；零值表示无效节点。 */
typedef uint64_t gneiss_scene_node_id;
/** 已加载场景实例的不透明句柄；由所属 Application 独占。 */
typedef uint64_t gneiss_scene_instance;
/** Prefab 刷新事务令牌；由所属场景实例独占。 */
typedef uint64_t gneiss_scene_prefab_refresh_token;

#define GNEISS_NULL_SCENE_NODE_ID UINT64_C(0)
#define GNEISS_NULL_SCENE_INSTANCE UINT64_C(0)
#define GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN UINT64_C(0)

/** 迁移期组件与作者状态位；原生类型见 src/engine/core/scene_types.hpp。 */
#define GNEISS_SCENE_NODE_COMPONENT_CAMERA UINT32_C(1)
#define GNEISS_SCENE_NODE_COMPONENT_MESH_RENDERER UINT32_C(2)
#define GNEISS_SCENE_NODE_COMPONENT_PRIMARY_CAMERA UINT32_C(4)
#define GNEISS_SCENE_SUBTREE_MAX_NODES UINT64_C(4096)
#define GNEISS_SCENE_PREFAB_NODE_INSTANCE_ROOT UINT32_C(1)
#define GNEISS_SCENE_PREFAB_NODE_SOURCE_READ_ONLY UINT32_C(2)
#define GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN UINT32_C(4)
#define GNEISS_SCENE_PREFAB_NODE_ROTATION_OVERRIDDEN UINT32_C(8)
#define GNEISS_SCENE_PREFAB_NODE_SCALE_OVERRIDDEN UINT32_C(16)

#endif
