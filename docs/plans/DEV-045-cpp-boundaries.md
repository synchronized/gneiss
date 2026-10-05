<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# DEV-045：内部 C++ 分层与完整包装实施计划

## 状态与依据

2026-10-05 已开始接口审计和 Core 迁移；首轮证据见
[M-292～M-293 记录](../records/M-292-293-cpp-boundary-foundation.md)。World 内部入口迁移见
[World 阶段记录](../records/M-294-world-cpp-boundary.md)，Application 拆分见
[Application 阶段记录](../records/M-294-application-boundary.md)。Reflection 迁移见
[Reflection 阶段记录](../records/M-295-reflection-boundary.md)。全部 C 入口集中见
[C 边界记录](../records/M-295-c-boundary-completion.md)；语义配置与其余模块仍在实施。范围和门槛以
[VER-045](VER-045-0.45.0-cpp-boundaries.md) 为准；目录与所有权以
[ADR-053](../decisions/ADR-053-cpp-core-and-c-abi.md) 与
[ADR-054](../decisions/ADR-054-source-layout-and-host-boundaries.md) 为准。后者的精简布局已确认，
Core、Platform、Asset 与 Function 已执行物理迁移；C ABI 已迁至 Engine，Editor/Apps 拆分继续按计划推进。

## M-292：审计与冻结基线

- 枚举公共 `.h` 导出函数、结构、枚举、宏、回调和 `.hpp` 包装。生成可复核清单：C 符号、
  C++ 入口、拥有/借用、父资源、线程限制、成功/错误/寿命测试、缺口。
- 工具枚举配合人工语义审查；不能用 grep 同名计数判断 RAII 完整。
- 对 Core、Application、World、Scene、Render、Asset、Input、Reflection、Log、Game Module
  逐一记录内部入口、唯一状态所有者、允许依赖与现有越界。
- 审计 `application_api.cpp`、`world_api.cpp`、`application_*_internal.h` 及 apps 对 src 的依赖。
  私有 C++ 导出分为应公开能力、同版本宿主接口和可消除的越界，不盲目全部公开。
- 记录 0.44 导出与结构布局、安装目标和 C/C++ 示例，生成迁移前基线；实现前保存现有测试结果。
- 阅读当前锁定 Granit 的内部实现、C 入口、拥有与借用包装，记录可借鉴点；不因参考而修改或升级它。

## M-293：小模块验证

首选 Core 的结果/版本与 Log，验证目录、错误转换、回调寿命和 C++ 包装路径。
如 Log 审计显示与宿主强耦合，先完成 Core，再选择边界更独立的模块并记录原因。

ABI 入口已按 ADR-054 迁至 `src/engine/api/`；
核心实现留在所属模块；建立第一版依赖检查，使用故意违规的测试
验证检查能失败。更新 CMake 私有依赖、独立头和安装消费者，确认没有新增第三方传播或重复符号。

## M-294：Application、World 与 Scene

1. 将句柄解析与公开描述校验和核心生命周期、World 操作、场景逻辑分开，注册表保持唯一。
2. Application 只装配与协调；服务资源仍由对应 Service 管理。Scene 节点只关联实体 ID。
3. 收口宿主内部访问：公共能力走完整 SDK，专用能力走明确的私有接口；去掉因便利直接访问状态的调用。
4. 覆盖创建失败回滚、父子销毁顺序、移动后源对象、重复销毁、跨域 RID、旧 generation 和非所属线程调用。
5. 迁移一个模块即运行相关场景与宿主回归，不等全部搬完才测试。

## M-295：Render、Asset 与其他模块

Render 快照、PNG 解码与 Granit 目录子项见 [迁移记录](../records/M-295-render-asset-layout.md)；
资产 CPU 准备与发布已拆分；其余接口语义与配置审计仍待完成。平台工具反向链接清理见
[链接边界记录](../records/M-295-platform-link-boundary.md)；Core 迁移见
[基础层记录](../records/M-295-core-layout.md)，日志投递拆分见
[日志边界记录](../records/M-295-log-sink-boundary.md)。Asset 基础能力迁移与上传服务归属见
[资产边界记录](../records/M-295-asset-layout.md)。
CPU 准备已与资源发布分开编译并建立独立链接验证，见
[准备接口记录](../records/M-295-render-preparation.md)；材质准备值已去除 RID，见
[材质参数记录](../records/M-295-material-parameters.md)。网格与纹理准备值已归入 Asset，见
[准备数据记录](../records/M-295-prepared-data.md)。CPU 准备与解析已归入 Asset，Render 传入发布预算，
见 [Asset 准备闭环记录](../records/M-295-asset-preparation.md)。
Function 六模块迁移与新路径边界验证见 [迁移记录](../records/M-295-function-layout.md)。
C ABI 目录与清单检查迁移见 [适配层记录](../records/M-295-api-layout.md)。
Editor UI 的实现与入口分离见 [UI 迁移记录](../records/M-295-editor-ui-layout.md)。
资产服务迁移与验证见 [资产服务记录](../records/M-295-editor-assets-layout.md)。

- 将解码、格式解析、资源加载与 GPU 生命周期按 ADR 分开，先记录允许依赖再搬文件。
- Granit 适配进入 Render 私有后端，Platform 的窗口适配维持独立所有权。
- 保持 CPU 准备、上传回执、纹理租约、场景候选和预算计数语义；不借重构重新实现加载系统。
- 完成 Input、Reflection、Log、Game Module 等余下模块；明确插件 ABI 边界允许的 C 调用。
- 内部头按模块迁移为 `.hpp`，调整测试归属和 target，不进行无关格式化。

### 精简布局迁移映射与顺序

目标目录树只在 ADR-054 维护；下表用于核对原有代码的迁移范围；完成项单独标明，其他项仍待迁移。

| 当前代码 | 目标归属与拆分要求 |
| --- | --- |
| 原 `src/api/c/` | 已迁至 `src/engine/api/`；保持公共头、C 导出与调用方向 |
| 原 `src/application/`、`world/`、`scene/`、`render/`、`input/`、`game/` | 已迁至 `src/engine/function/` 下同名模块，内部头统一 `.hpp`；Granit 后端随 Render |
| 原 `src/asset/` | 基础能力已迁至 `src/engine/asset/`；纹理加载与上传事务归 `src/engine/function/render/texture_load_service.hpp`，CPU 准备与解析已归入 Asset，Render 仅传入发布预算 |
| 原 `src/core/`、`reflection/`、`log/` | 已迁入 `src/engine/core/`；投递器使用 C++ 消息与事件视图，Application 回调适配留在所属模块 |
| Render 中的数学代码 | 通用数学迁 Core；相机语义、后端投影适配仍留所属功能，逐文件判断 |
| 原 `src/platform/`、`io/`、`process/`、`ipc/` | 已迁至 `src/engine/platform/`，内部头改为 `.hpp`；编辑器/运行宿主协议仍留 Apps，平台语义描述与命名空间继续审计 |
| `apps/editor/` | UI、主题、ImGui 适配、字体与资产服务已迁至 `src/editor/`；会话与面板编排仍待迁移，程序入口与启动配置保留在 `apps/editor/` |
| `apps/runtime/` | 程序入口与宿主控制保留在 `apps/runtime/`；可复用引擎能力回归 Engine |
| `apps/common/` | 逐项核定所有者；运行时通用能力归 Engine，纯宿主共用代码按实际需要保留私有共用目标，不原样下沉 Core |
| `src/tooling/`、`tools/assetc/` | 离线实现留 `src/tooling/`；assetc 入口、CLI 进入根目录的 `apps/assetc/` |
| `tools/performance/`、`tools/sanitizers/` 等 | 仓库维护脚本归根 `scripts/`；可编译验证/基准程序归对应 tests，不能仅改名假定全是脚本 |
| 测试、示例、构建与检查 | 按被测模块更新路径和私有 include；公共 SDK 示例与安装消费路径保持兼容 |

1. 先审计包含与 target 依赖，记录每个模块的唯一状态所有者。拆开资产 CPU 准备、GPU 发布与
   Function 协调，消除 Platform/Core 的反向链接；不把现有循环依赖带进新目录。
2. 按 Core/Platform、Asset、Function 逐组迁移，伴随更新 CMake、内部 `.hpp` 路径及正反例检查。
   不复制实现作为过渡，不把整个 src 作为所有目标的公共包含目录。
3. 拆分 src 下的 Editor、Tooling 实现与根目录 Apps 入口；收口 `apps/common`，确认运行宿主不链接编辑器。
   保持进程协议、工作目录、资源定位和现有可执行文件名不变。
4. 更新测试、维护脚本及文档的实际路径；每组完成相关验证后本地提交，最终统一完成发布矩阵。
   未迁移前的历史 Record 与版本归档保留原路径事实，当前 Guide/Concept 随实际迁移更新。

验收需包含错误方向的故意违规反例：Engine→Editor/Apps、Asset→Render、Render→World、
内部模块→公共 RAII，以及 Core/Platform 的循环。工具可用文本检查辅助，但还需构建与链接证据。
本任务不增加插件加载器、插件目录或编辑器公开 API；仅建立独立实现与入口边界。

## M-296：完整 C++ SDK

与各模块迁移同步补包装，本阶段统一核对覆盖清单，所有缺口必须关闭。既有拥有者的关闭与转移
已完成子项，见 [生命周期记录](../records/M-296-owner-lifecycle.md)；其余包装和类型/回调审计继续进行。

- 每个公共 C 功能都有强类型入口或明确的值类型映射；现有 C++ 调用方式尽量兼容。
- 拥有/借用类型分开，测试 move、reset、release、失败创建与父句柄先失效。
- 字符串、数组、回调返回数据明确有效期；回调适配测试异常隔离、注销及上下文销毁。
- 异步请求测试取消、迟到结果、关闭与借用失效；不得让 RAII 析构隐式死锁。
- 示例使用安装后的公共 C++ SDK，覆盖资源创建、场景、输入、反射、日志与模块工作流，不能依赖 src。
- 新增 C 接口但未更新映射清单、包装或测试时检查失败；无意义包装数量不作为覆盖指标。

## M-297：验证与交付

按 [构建指南](../guides/building.md) 验证共享与静态库、安装 C/C++ consumer、C11/C++20 头独立编译、
导出和布局基线。运行受影响模块单测，再跑完整 CTest、Editor/Runtime 工作流与真实 GPU 像素测试。
保留隐藏测试窗口；窗口交互专项除外。大场景加载/取消/切换回归沿用 0.44 输入，不重新包装成性能优化。

检查安装目录不包含内部头，内部模块不依赖公共 RAII，C 入口无核心业务重复实现；检查私有链接
传播、循环依赖、跨层包含和第三方类型泄漏。平台矩阵按实际受影响范围触发，不能只凭文本检查验收。

文档分别更新当前 Reference、Concept、迁移 Guide；结果写 Record。发布后再添加版本归档。
在一个特性分支完成设计、迁移、验证和收口；每组可验证改动做本地提交，整体就绪后一个 PR。

## 未决项与风险

- 完整接口数量和具体缺口由 M-292 得出，当前不声称已完成全量审计。
- 具体模块 target 划分、私有导出清单及强耦合处拆分顺序由依赖图确定。
- 父服务销毁与线程亲和性可能暴露已有缺陷；先固定契约与复现，再最小修复并独立记录。
- 如果必须改变现有 ABI 或增加新的强持有关系，先补 ADR 与迁移决策，不默默扩大重构范围。
- GAMES104 分层参考及精简布局已确认；具体文件的归属需按依赖审计决定，不机械按旧目录整体搬移。
- 平台工具对 Engine 的反向链接已移除；宿主共用代码仍含工程与通信职责。
  目录迁移前继续核定依赖与所有者，
  不为实现四层图而新增空模块、重复实现或永久白名单。
