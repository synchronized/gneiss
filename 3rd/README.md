<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 第三方依赖

本目录保存 Gneiss 可复现构建所需的第三方源码。除 Gneiss 自己的 CMake 包装外，不直接修改上游
源码。

| 依赖 | 版本 | 来源 | 许可证 | 用途 |
| --- | --- | --- | --- | --- |
| EnTT | 3.15.0 | <https://github.com/skypjack/entt/tree/v3.15.0> | MIT | 内部 ECS 存储与查询 |
| yyjson | 0.12.0 | <https://github.com/ibireme/yyjson/tree/0.12.0> | MIT | 内部 JSON 读写与错误定位 |

第三方源码通过 Git submodule 锁定。首次检出仓库后执行：

```sh
git submodule update --init --recursive
```

EnTT 只作为 Gneiss 内部实现依赖，不进入公共头文件、C ABI、持久化场景格式或稳定类型标识。
编辑器、反射和序列化由未来的 Gneiss Schema 层提供。

yyjson 固定构建为内部静态库，不安装、不传播其头文件，也不决定 Gneiss 的公共 Schema 类型。
