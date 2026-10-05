// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

// 测试专用动态库，验证平台层可加载不依赖 Engine 的原生模块。
int gneiss_test_library_value(void) { return 42; }
