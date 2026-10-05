<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 源码目录与模块所有权

## 目的

本文描述当前实际源码布局，不把已批准的迁移目标当成已实现。0.45 的精简目标目录与宿主边界见
[ADR-054](../decisions/ADR-054-source-layout-and-host-boundaries.md)，迁移映射与验收见
[DEV-045](../plans/DEV-045-cpp-boundaries.md)。Core、Platform、Asset 与 Function 已迁入 `src/engine/`，
C ABI 适配与 `src/editor/` 尚未完成迁移；
`apps/` 保持位于仓库根目录，编辑器实现与入口的拆分尚未完成。

总体分层以[总体架构](architecture.md)为准，代码与文档规范分别以
[C/C++ 代码风格](../guides/coding-style.md)和[项目文档规范](../../DOCUMENTATION_GUIDE.md)为准。

## 当前顶层目录

| 目录 | 所有权与职责 |
| --- | --- |
| `include/gneiss/` | 稳定的 C11 公共接口及其轻量 C++20 包装 |
| `src/` | 内部运行时实现、第三方适配及离线处理 |
| `apps/` | 当前 Editor、Runtime 宿主及共用代码 |
| `tools/` | 当前 assetc 程序、性能与检查工具 |
| `tests/` | 公共接口、内部行为、生命周期与集成验证 |
| `examples/` | 使用公共接口构建的独立最小示例 |
| `docs/` | Guide、Reference、Concept、Plan、ADR 和执行记录 |
| `3rd/` | 锁定版本并与自有代码隔离的第三方依赖 |
| `cmake/` | 项目构建策略和可复用的 CMake 模块 |

当前 `gneiss_engine` 的四层实现已集中在 `src/engine/`，C ABI 适配仍位于 `src/api/c/`。
Runtime 宿主与 Editor 分别位于 `apps/runtime/` 和 `apps/editor/`；两者可以依赖 Engine Library，
Engine Library 不得反向依赖它们。Editor 的 ImGui Context、字体、主题、DockSpace 和通用控件由
不安装、不导出的 `gneiss_editor_ui` 内部静态库统一管理，业务面板状态仍由 Editor 应用层持有。

公共头目录按能力组织，但 `.h` 与 `.hpp` 始终成对维护。内部源码目录按拥有运行时状态的模块组织，
不机械复制公共头目录。

## 运行时模块

以下为当前实现路径；按各模块实际迁移结果更新。

| 模块 | 目录 | 职责 | 状态 |
| --- | --- | --- | --- |
| Core | `src/engine/core/` | 结果、RID、Service 注册等无领域倾向的基础设施 | 已存在 |
| World | `src/engine/function/world/` | World、Entity、System 与 EnTT 适配 | 已存在 |
| Scene | `src/engine/function/scene/` | Scene Tree、节点映射与层级 Transform | 已存在 |
| Application | `src/engine/function/application/` | 顶层生命周期、初始化回滚和主循环编排 | 已存在 |
| Platform | `src/engine/platform/` | 窗口、IO、IPC、子进程和动态库等平台能力的隔离 | 已存在 |
| Asset | `src/engine/asset/` | VFS、格式、CPU 解码与缓存 | 已迁移 |
| Render | `src/engine/function/render/` | 渲染输入快照、资源 RID、帧包与执行器 | 已存在 |
| Input | `src/engine/function/input/` | 输入状态与动作映射 | 已迁移 |
| Game | `src/engine/function/game/` | Game Module 上下文与宿主桥接 | 已迁移 |
| Reflection | `src/engine/core/reflection/` | 唯一类型注册表与属性访问 | 已存在 |
| Log | `src/engine/core/log/` | 通用文本校验与异步事件投递；Application 独立适配公共回调 | 已迁移 |
| C ABI | `src/api/c/` | 公开描述适配、异常隔离及内部入口委托 | 已存在 |
| Tooling | `src/tooling/` | 离线资产处理实现 | 已存在 |
| Granit 后端 | `src/engine/function/render/backend/granit/` | Granit 类型、调用和错误转换的隔离 | 已存在 |

Platform 的 Granit Window 适配位于 `src/engine/platform/granit/`；`src/engine/function/render/backend/granit/` 只负责
渲染后端，两者不共享原生对象所有权。渲染快照的值类型位于 `src/engine/function/render/render_snapshot.hpp`，
World 的提取函数位于 `src/engine/function/world/render_snapshot.hpp`，Render 不反向包含 World/Scene/Application。
`src/engine/asset/png_decoder.hpp` 提供 CPU 解码，Cook 与运行时加载共用；解码不依赖渲染服务。

纹理加载与上传事务由 `src/engine/function/render/texture_load_service.hpp` 组织，属于 Render 功能层；
其依赖 Asset 的 VFS，但 Asset 不反向包含 Render。

`render_asset_loader` 暂仍位于 Render，它连接 Asset 缓存租约与 Render 资源发布，并不只是格式
解码器。CPU 准备和解析实现位于 `src/engine/asset/asset_preparation.hpp/.cpp`，
Render 的准备适配只传入发布资源的预算。三类准备数据均使用 Asset 值类型，材质依赖仅用 URI，
发布边界绑定纹理 RID 并移动纹理数组；上传弱引用仍由 Render 管理。Asset 不包含 Render 实现，
独立准备测试不链接 Engine、缓存或资源服务。其余迁移见 [0.45 实施计划](../plans/DEV-045-cpp-boundaries.md)。

当前测试分布在 tests 根目录及 `tests/core/`、`tests/world/`、`tests/scene/` 等模块目录。
公共头独立编译位于 `tests/headers/`，安装消费者位于 `tests/consumer/`；跨模块工作流还通过
CMake 脚本验证。尚未统一迁移到新的测试层级，不创建空的 integration 目录。

## 演进规则

- 新目录必须对应一个已经开始实施的模块或工具，不添加占位文件。
- 新模块先确认职责、拥有的状态、公开接口和允许依赖；跨层变化使用 ADR 记录。
- 第三方后端放在所属 Service 下面，不建立可被所有模块随意依赖的通用 `backend/`。
- 平台差异集中在 `src/engine/platform/` 或具体后端目录，不散布到 World、Scene 和业务组件。
- 单个实现文件只被一个模块使用时留在该模块内部；只有形成稳定跨模块契约后才提升为公共接口。
- 目录调整应伴随真实代码迁移和验证，不单独进行大规模结构美化。
