<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 从 0.10.0 迁移到 0.11.0

## 适用场景

本指南适用于通过 CMake package、共享库或静态库消费 Gneiss 0.10.0，并准备升级到 0.11.0 的工程。
0.11.0 统一了 Engine Library 与工程运行宿主的命名；C/C++ 源码接口和持久格式不因本次更名改变。

## 构建目标更名

下游 CMake target 从 `gneiss::gneiss` 改为 `gneiss::engine`：

```cmake
find_package(gneiss CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE gneiss::engine)
```

0.11.0 不继续导出旧 target 别名。升级后应删除旧 CMake 配置缓存并重新配置，避免构建目录继续引用
0.10.0 的导出文件。

## 二进制产物更名

| 0.10.0 | 0.11.0 | 含义 |
| --- | --- | --- |
| `gneiss.dll` / `libgneiss.so` / `libgneiss.a` | `gneiss_engine.dll` / `libgneiss_engine.so` / `libgneiss_engine.a` | 完整 Engine Library |
| 无通用工程宿主 | `gneiss_runtime` | 根据工程描述运行启动场景的宿主 |
| `gneiss_editor` | `gneiss_editor` | Editor，名称不变 |

复制、打包、`PATH`、RPATH 或部署脚本若硬编码旧库文件名，需要同步修改。CMake target Consumer 通常
无需直接处理文件名。

## 不受影响的接口

- C API 继续使用 `gneiss_` 前缀，头文件路径仍为 `include/gneiss/`。
- C++20 包装继续位于 `gneiss` 命名空间。
- `find_package(gneiss CONFIG REQUIRED)` 和 package 名称保持不变。
- `gneiss.project.json`、场景、动作映射、资产索引和 Mesh Binary 格式保持原版本。
- 公共函数符号、结果码和 RID 值语义不因库文件更名改变。

## Runtime 宿主

需要通用工程运行入口时显式启用：

```sh
cmake -S . -B build \
  -DGNEISS_ENABLE_GRANIT_PLATFORM=ON \
  -DGNEISS_BUILD_RUNTIME=ON
cmake --build build --target gneiss_runtime
```

运行方式：

```sh
gneiss_runtime --project /path/to/project
```

`gneiss_runtime` 是依赖 `gneiss_engine` 的薄宿主，不是第二套 Engine Library。

## 验证

1. 删除或新建 CMake 构建目录并重新配置。
2. 确认下游只链接 `gneiss::engine`。
3. 共享库构建确认部署目录包含新的 Engine Library 文件名。
4. 运行既有 C/C++ Consumer 和场景加载测试。
5. 使用 `gneiss_runtime --smoke --project <工程根>` 验证工程启动场景。
