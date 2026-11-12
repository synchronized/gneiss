<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Application 与 World 最小示例

示例使用仓内 C++20 原生接口创建 Application、场景和对象。借用 game_object 修改局部变换，
第三次更新请求退出；检查结果后显式关闭 Application。默认无窗口，不初始化 GPU。

```sh
cmake --build --preset windows-clang-debug --target gneiss_minimal_example
ctest --test-dir build/windows-clang-debug -R '^gneiss.minimal-example$' --output-on-failure
```

完整流程位于一个 [main.cpp](../../examples/minimal/main.cpp)，没有单独的回调包装对象。
图形加载与测量见[示例入口](../../examples/README.md)。
