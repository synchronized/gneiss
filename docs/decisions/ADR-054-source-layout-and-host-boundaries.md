<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-054：精简源码布局与宿主边界

## 状态与背景

2026-10-05 已接受，纳入 0.45；本次只确认文档，源码尚未按新布局迁移。
本决策替代 [ADR-053](ADR-053-cpp-core-and-c-abi.md) 的旧目标目录，保留其接口、ABI 与所有权约束。
实施顺序与迁移映射见 [DEV-045](../plans/DEV-045-cpp-boundaries.md)，当前路径见
[源码目录说明](../concepts/repository-layout.md)。

参考 GAMES104 的功能、资源、基础和平台分层；Engine/Editor/Runtime 是库与宿主的组织维度，
内部 C++ / C ABI / C++ SDK 是接口维度，二者与架构分层并存。Tool 层对应创作工具；Runtime
是使用引擎的运行宿主，不因位于 apps 就成为编辑工具。不照搬课程全部目录或实现。

## 目标布局

以下是目标路径的唯一完整目录树，尚未实现。只列有明确职责的层级，细分目录随真实规模建立。

```text
gneiss/
├─ include/gneiss/          # 公共 C11 ABI 与 C++20 SDK，保留现有头路径
├─ src/
│  ├─ engine/              # 可独立使用的引擎库
│  │  ├─ api/              # 公共 C ABI 适配，按功能拆分 *_api.cpp
│  │  ├─ function/
│  │  │  ├─ application/   # 生命周期、服务装配与跨模块协调
│  │  │  ├─ world/         # ECS、组件与系统
│  │  │  ├─ scene/         # 场景树、实例与 Prefab
│  │  │  ├─ render/        # 渲染资源、帧包、执行器与所属后端
│  │  │  ├─ input/
│  │  │  └─ game/          # 游戏模块接入与上下文
│  │  ├─ asset/            # 格式、CPU 解码、VFS、缓存与准备
│  │  ├─ core/             # RID、任务、通用数学、反射与日志
│  │  └─ platform/         # 窗口、IO、进程及系统适配
│  ├─ editor/              # 编辑器实现，独立于启动入口
│  ├─ tooling/             # 可复用的导入、Cook 等离线处理
│  └─ apps/                # 可执行程序入口、配置与宿主装配
│     ├─ editor/
│     ├─ runtime/
│     └─ assetc/
├─ tests/                  # 按被测模块组织；保留公共头、消费者与集成验证
├─ examples/
├─ templates/
├─ docs/
├─ cmake/
├─ scripts/                # 仓库开发、检查与维护脚本
├─ abi/
└─ 3rd/
```

`api/` 当前仅承载 C 适配，不额外保留只有一个子目录的 `api/c/`。内部以 `.hpp`/`.cpp` 组织，
公共 `.h`/`.hpp` 路径保持兼容。不预建 asset 的 formats/codecs/cache/loading、编辑器面板细分
或插件目录。目录归属不自动要求新增动态库，也不改变现有可执行文件名、安装目标和消费方式。

## 所有权与依赖

- Engine 不依赖 Editor、Tooling 或 Apps；Editor 可使用 Engine 和离线 Tooling，Tooling 不依赖
  Editor/Apps。Apps 装配相应实现，其他模块不反向包含 Apps；启动文件不承担可复用业务规则。
- Function 消费 Asset、Core、Platform 的内部契约；同在 Function 下不意味着可任意互相依赖。
  Scene 关联实体 ID、ECS 保存数据/RID、Service 管理后端的既有约束继续有效。
- Asset 负责无 GPU 对象的格式、解码、缓存与 CPU 准备；GPU 上传、候选及发布由 Render 负责，
  跨两者的事务协调放在 Function 的所属协调模块，禁止 Asset 反向依赖 Render/World/Scene。
- Core 不认识资产业务、游戏世界、渲染或编辑器。Platform 可使用 Core 的基础契约，但 Core 不
  反向依赖整个 Platform；OS 专用实现隔离在私有实现文件。不得形成 target 或包含循环。
- Granit 渲染实现留在 Render 的私有 `backend/granit/`；窗口适配归 Platform。不建立供所有
  模块访问的通用后端目录；项目协议和编辑器语义不得因使用 IO/IPC 而下沉到 Platform。
- API 适配委托 Engine 内部实现，内部不绕回公共 ABI；动态 Game Module 等真实边界单独标明。
  构建私有依赖与违规反例检查必须随迁移更新，不能只通过改路径表示分层完成。

## 插件扩展边界

本版不实现插件框架，不创建占位 API。编辑器实现与入口拆开，为后续独立扩展服务保留边界。
将来引擎扩展与编辑器扩展按能力分开：编辑器 SDK 可独立放在 `include/gneiss/editor/`，运行宿主
不依赖它。插件通过公开接口访问能力，不直接包含 src。版本协商、回调注销、任务结束与卸载安全
需独立 ADR；先评估已有 Game Module ABI 的复用范围，不提前承诺通用插件 ABI。

## 取舍

采用用途分区和 Engine 内部四层，便于定位所有者；保留 asset 名称，避免与现有资源模块重名。
暂不采用更深的目录树，也不把每个模块变成独立库。迁移成本包括 CMake、内部 include、宿主路径、
脚本、测试夹具与安装验证；一次迁移一个可验证模块，保留唯一实现，不以复制源码过渡。
