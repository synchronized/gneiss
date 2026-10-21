// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/tasks/task_scheduler.hpp"
#if __has_include("engine/core/tasks/task_platform.hpp")
#error "任务消费者获得了私有后端或上层模块头"
#endif
#if __has_include("runtime/application.hpp")
#error "任务消费者获得了私有后端或上层模块头"
#endif
#if __has_include("editor/editor_session.hpp")
#error "任务消费者获得了私有后端或上层模块头"
#endif
int main() { return 0; }
