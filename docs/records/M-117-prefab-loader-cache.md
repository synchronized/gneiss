<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-117 Prefab Loader 与依赖缓存实施记录

## 结论

M-117 已把 Prefab 描述接入 VFS 与统一资源缓存。调用方以规范 `asset://` URI 获取只读租约；同一
URI 的重复获取复用解析结果，租约释放后可由现有缓存回收机制清理。

## 当前行为

- Loader 在读取前校验 URI，拒绝目录逃逸和非规范资产地址。
- 文件读取与解析失败返回结构化结果、字段路径、字节偏移和中文诊断，不缓存失败结果。
- Prefab 节点继续复用场景 Schema 对 UUID、层级、Transform、Mesh/Material URI 和组件进行校验。
- Loader 从 Mesh Renderer 收集 Mesh 与 Material 依赖 URI，排序并去重后保存在只读描述中。
- 缓存只保存自有 `prefab_description`，公共接口和缓存边界均不暴露 yyjson 类型。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| VFS 读取并解析 Prefab | 通过 |
| Mesh/Material 依赖收集、排序与去重 | 通过 |
| 相同 URI 获取两份租约只读取一次文件 | 通过 |
| 目录逃逸 URI 拒绝且不返回租约 | 通过 |
| Windows Clang Debug 相关专项测试 | 3/3 通过 |

M-118 将消费只读租约，并负责 Runtime 子树、渲染依赖租约及失败回滚；本里程碑不创建 Runtime
节点或后端资源。
