<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-069：资源模块与运行时组装边界

- 状态：已接受，分批实施
- 日期：2026-10-21

2026-10-22：CAORS 原生请求和基类实现选择已由 [ADR-071](ADR-071-example-asset-manager.md)
取代；本页模块所有权、CPU/GPU 和 Runtime/Editor 边界继续有效。

## 背景

engine/asset 与 function/asset 分散资源能力，统一管理器仍混合缓存、渲染配置、
上传通道与应用回调。局部所有权修正没有消除跨层协议，单纯移动目录不能解决问题。

## 决策

- 合并为 engine/resource，负责 CPU 资源描述、加载、缓存和引用；CAORS 是内部实现。
- function 保留 Input、Scene、World、Render 的运行时行为；GPU 生命周期归 Render。
- Application 的系统组装与场景切换流程归 src/runtime，apps 保留薄入口。
- 依赖 Application 的游戏上下文及 C 适配同属 Runtime；Engine 不反向包含 Runtime。
  混合提供资源和 Application 入口的渲染、场景适配文件暂整体归 runtime/api，复用
  engine/api 的值转换辅助；不为目录迁移修改公共符号，也不新增动态库边界。
- Editor 文档管理编辑状态，不复制运行时场景树。保留一个 World 多个 Scene。
- 资产管理器不执行场景或编辑回调；应用层在修改成功后提交已准备的资源版本选择。
- 不新增全局 locator，不并行实现第二套资源状态机，不改变公共 SDK 命名规则。
- 复用已有对象；最终名称与文件以实际职责确定，不为布局图增加包装类。

目标文件与对象布局、实施批次及验收统一见
[模块布局计划](../plans/PLAN-051-module-layout.md)。本决策细化 ADR-068 的资源归属，
替代其将资源管理长期放在 function/asset 的目录安排；保留其接入与平台契约。
ADR-064 的共享资产和一 World 多 Scene 决策保持有效。

## 影响

先移除反向调用，再逐步分离 CPU 描述与 GPU 资源，最后调整目录和构建边界。
迁移期间保留可验证的候选隔离、失败原子性和取消行为，不能宣称目标已经全部实现。

## 替代方案

- 只搬文件：不能修复反向依赖与多个缓存协议。
- 使用全局 locator：缩短获取路径，但隐藏依赖，无法解决职责混合。
- 一次重写全部资源系统：回归范围过大，难以保留生命周期语义；采用分批替换并删除旧路径。

## 命名空间补充决策（2026-10-21）

用户确认命名空间与模块职责对应：Engine 使用 gneiss::engine 及领域子空间，Runtime
使用 gneiss::runtime，Editor 使用 gneiss::editor。去除业务模块的 xxx_internal 命名，
不保留别名；内部 C++ 接口不承诺二进制兼容，须一起重编译。命名空间不替代依赖检查。
分批迁移与冲突处理以[模块布局计划](../plans/PLAN-051-module-layout.md)为准。

## 文档编辑生命周期收敛

复用既有对象组成 document_editing：拥有 scene_document、撤销历史、资产编辑和 Prefab
编辑控制器。它不拥有 Application/执行器，不增加操作状态机，不承接场景域切换或保存提示 UI。
取消后必须继续推进至终态；关闭先确认无在途编辑，再释放历史、最后关闭文档。
宿主负责驱动执行器并保证 Application 存活，不能在析构中隐藏阻塞等待。

## 宿主模块决策（方案 B）

用户确认 Apps 只保留可执行入口；宿主启动、项目工具、子进程和 IPC 适配迁入
src/host/common、src/host/editor、src/host/runtime。Host 依赖 Editor、Runtime、Platform，
后三者不反向依赖 Host。Editor 业务与宿主装配必须区分，迁移文件不代表 UI 装配拆分完成。
不新增 DLL，不改变既有进程协议；运行目标与命令行行为保持不变。

## 原生资源请求依赖（2026-10-21）

用户确认采用薄 asset_request<T>，允许内部 C++ 资源请求及其 Runtime 消费者依赖 CAORS。
公共 C ABI、安装头和输入实时服务继续隔离第三方类型。请求只拥有 CAORS 订阅，复用其
状态、强引用和取消，不重复创建任务、缓存或完成状态机；按移动语义转移取消责任。
取资源与应用资源分离：Runtime 负责输入映射复制、验证和替换。删除专用 action_map_request
虚接口及其工厂，不为每个资源类型新增请求实现。原 CPU 批次准备的 URI/type 描述改名
asset_preparation_source，与异步订阅明确区分。

## CPU 文档与加载适配分离（2026-10-21）

资源类型属于 resource/types，即使继承 CAORS 原生 resource，也不因此归入 backend。
材质来源描述不依赖解析器；纯字节解码属于 loaders；Gneiss 的来源身份、内容版本索引及
只读租约属于 cache。backend/caors 只负责把加载执行协议接到这些类型和解码逻辑。
准备结果与渲染保活字段使用具体 material_document_lease，不再通过空虚基类擦除类型。
这不改变整批来源复验、固定版本选择和 GPU 服务销毁责任，也不增加缓存或异步状态。
