// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/game_module.h>

#if defined(GNEISS_APPLICATION_H_) || defined(GNEISS_WORLD_H_) || defined(GNEISS_REFLECTION_H_) || \
    defined(GNEISS_INPUT_H_) || defined(GNEISS_SCENE_H_)
#error "游戏模块协议不得传递引入完整引擎 SDK"
#endif
