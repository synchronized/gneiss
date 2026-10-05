<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 源码目录与模块所有权

## 目的

目录用于表达模块所有权和依赖方向，而不是提前枚举全部未来功能。当前只创建已有真实实现的目录；
规划中的目录在对应功能进入开发后再落地，避免空目录和未经验证的模块边界。

总体分层以[总体架构](architecture.md)为准，代码与文档规范分别以
[C/C++ 代码风格](../guides/coding-style.md)和[项目文档规范](../../DOCUMENTATION_GUIDE.md)为准。

## 当前顶层目录

| 目录 | 所有权与职责 |
| --- | --- |
| `include/gneiss/` | 稳定的 C11 公共接口及其轻量 C++20 包装 |
| `src/` | 不进入公共 ABI 的运行时实现和第三方适配 |
| `tests/` | 公共接口、内部行为、生命周期与集成验证 |
| `examples/` | 使用公共接口构建的独立最小示例 |
| `docs/` | Guide、Reference、Concept、Plan、ADR 和执行记录 |
| `3rd/` | 锁定版本并与自有代码隔离的第三方依赖 |
| `cmake/` | 项目构建策略和可复用的 CMake 模块 |

`gneiss_engine` 库及其 `src/` 实现就是完整 Engine Library，不额外建立职责宽泛的 `src/engine/`。
Runtime 宿主与 Editor 分别位于 `apps/runtime/` 和 `apps/editor/`；两者可以依赖 Engine Library，
Engine Library 不得反向依赖它们。Editor 的 ImGui Context、字体、主题、DockSpace 和通用控件由
不安装、不导出的 `gneiss_editor_ui` 内部静态库统一管理，业务面板状态仍由 Editor 应用层持有。

公共头目录按能力组织，但 `.h` 与 `.hpp` 始终成对维护。内部源码目录按拥有运行时状态的模块组织，
不机械复制公共头目录。

## 运行时模块

以下是随功能逐步形成的目标结构。表中“规划”只表示预留的所有权边界，不代表目录或能力已经存在。

| 模块 | 目录 | 职责 | 状态 |
| --- | --- | --- | --- |
| Core | `src/core/` | 结果、RID、Service 注册等无领域倾向的基础设施 | 已存在 |
| World | `src/world/` | World、Entity、System 与 EnTT 适配 | 已存在 |
| Scene | `src/scene/` | Scene Tree、节点映射与层级 Transform | 已存在 |
| Application | `src/application/` | 顶层生命周期、初始化回滚和主循环编排 | 已存在 |
| Platform | `src/platform/` | 窗口、事件和时间等平台能力的隔离 | 已存在 |
| Asset | `src/asset/` | VFS、格式、CPU 解码、缓存及加载任务 | 已存在 |
| Render | `src/render/` | 渲染输入快照、资源 RID、帧包与执行器 | 已存在 |
| Reflection | `src/reflection/` | 唯一类型注册表与属性访问 | 已存在 |
| C ABI | `src/api/c/` | 公开描述适配、异常隔离及内部入口委托 | 已存在 |
| Tooling | `src/tooling/` | 离线资产处理实现 | 已存在 |
| Granit 后端 | `src/render/backend/granit/` | Granit 类型、调用和错误转换的隔离 | 已存在 |

Platform 的 Granit Window 适配位于 `src/platform/granit/`；`src/render/backend/granit/` 只负责
渲染后端，两者不共享原生对象所有权。渲染快照的值类型位于 `src/render/render_snapshot.hpp`，
World 的提取函数位于 `src/world/render_snapshot.hpp`，Render 不反向包含 World/Scene/Application。
`src/asset/png_decoder.hpp` 提供 CPU 解码，Cook 与运行时加载共用；解码不依赖渲染服务。

`render_asset_loader` 暂仍位于 Render，它连接 Asset 缓存租约与 Render 资源发布，并不只是格式
解码器。其 CPU 准备接口与发布接口的进一步拆分属于 [0.45 实施计划](../plans/DEV-045-cpp-boundaries.md)，
不能把搬移 PNG 描述为整个加载系统分层已完成。

测试优先镜像被验证模块，例如 `tests/core/`、`tests/world/` 和 `tests/scene/`。公共头独立编译测试
统一位于 `tests/headers/`，跨模块生命周期与端到端测试进入 `tests/integration/`。只有出现对应测试
后才创建目录。

## 长期扩展边界

以下模块仍待真实用例和独立 Plan，不代表当前能力：

- `src/serialization/`：消费稳定 Schema，负责版本化数据格式、迁移和读写。
- `src/ui/`：正式 UI 运行时；临时诊断 UI 不自动成为该模块。
- `src/audio/`、`src/physics/`、`src/network/`、`src/script/`：按真实需求引入的独立 Service。

长期依赖方向固定为：Serialization 可以依赖 Reflection；Editor 可以依赖运行时、Reflection 和
Serialization；运行时核心不得反向依赖 Editor。Reflection 只描述类型，不负责具体文件格式；
Serialization 不把编辑器状态写入运行时契约。

## 演进规则

- 新目录必须对应一个已经开始实施的模块或工具，不添加占位文件。
- 新模块先确认职责、拥有的状态、公开接口和允许依赖；跨层变化使用 ADR 记录。
- 第三方后端放在所属 Service 下面，不建立可被所有模块随意依赖的通用 `backend/`。
- 平台差异集中在 `src/platform/` 或具体后端目录，不散布到 World、Scene 和业务组件。
- 单个实现文件只被一个模块使用时留在该模块内部；只有形成稳定跨模块契约后才提升为公共接口。
- 目录调整应伴随真实代码迁移和验证，不单独进行大规模结构美化。
