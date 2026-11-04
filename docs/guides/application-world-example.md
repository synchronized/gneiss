<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Application 与 World 示例

示例使用 C++20 编写，通过 C ABI 创建 Application、查询 World、创建实体及场景节点。
更新回调每帧将实体的 X 坐标增加一，第三帧请求退出；最后验证位置并释放 Application。

```sh
cmake --build --preset windows-clang-debug --target gneiss_application_world_example
ctest --test-dir build/windows-clang-debug -R '^gneiss.application_world_example$' --output-on-failure
```

[程序入口](../../examples/application_world/main.cpp)负责生命周期，
[更新回调](../../examples/application_world/scene_update.cpp)演示读取和修改运行数据。
局部 scope_exit 保障提前返回时清理，正常路径显式检查关闭结果。示例不依赖公共 C++ 对象包装。
