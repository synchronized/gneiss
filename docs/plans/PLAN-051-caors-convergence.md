<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 0.51：CAORS 资源路径收敛

## 目标与状态

后续模块归属与迁移顺序遵循[模块布局计划](PLAN-051-module-layout.md)；
下文代码入口表描述迁移前现状，不是长期目录决策。

用户已确认不长期维护按资源类型开启/关闭的两套路径。目标是 CAORS 成为资源基础依赖，
输入、材质、Mesh 复用统一资源持有协议；同步与异步接口复用实现，仅等待方式不同。
输入、材质与 Mesh 开关已删除，CAORS 核心与 libuv 装配已分离；同步入口及未注入缓存的
底层通道仍待收敛，本计划不宣称全部完成。

## 实施顺序

1. 先验证支持平台的基础依赖。CPU 资源不应依赖 libuv；原生 libuv 适配应独立装配，
   不能因为使用 CAORS handle 就要求后台线程或文件 I/O。
2. 将输入宿主的创建从“已有 asset_io_executor”条件中解耦。协作执行器也应使用相同请求、
   缓存、取消和采用协议；VFS 后端明确提供打开、读取、关闭能力，禁止伪装异步。
3. 合并同步加载入口，复用类型加载、固定版本和错误语义；同步成功仍表示加载完成，
   不改成仅提交请求，不在 update 内等待需要同一 update 推进的任务。
4. 移除输入、材质、Mesh 的功能开关与条件分支，以及关闭状态返回 unsupported 的测试；
   替换为原生线程、协作执行、来源变化、取消、失败和生命周期测试。
5. 验证共享/静态安装消费与目标平台，再继续纹理载荷迁移。

不增加替代的总开关来长期维持两套资产实现。Web 静态链接与原生共享链接属于平台部署选择，
不能因此分叉资源所有权或加载协议。当前 Web 支持范围不得在迁移时静默缩减。

## 上游修复与剩余障碍

2026-10-20，旧锁定提交在 Emscripten 5.0.6 下配置静态库成功、编译失败。
首批错误位于 registry、file_system_resolver、mount_file_system 的 atomic<shared_ptr>；
不是 Gneiss 的线程池或 libuv 链接错误。复现及上游验收见
[CAORS 无线程/Web 兼容需求](UPSTREAM-051-caors-web-convergence.md)。

输入宿主已与后台 libuv loop 的存在条件解耦；协作请求使用同一 CAORS 加载流程，
无异步打开能力的后端可显式选择在执行器中读取有界快照。原生无线程及 Emscripten MEMFS 输入验收已通过，
输入开关及相应构建限制已删除。协作模式不提供后台 I/O 或单步耗时保证。

## 后续实施

2026-10-21 上游已发布 0.7.0 Web 平台支持，Gneiss 更新依赖并复验原编译障碍。
结果见[升级验收记录](../records/2026-10-21-caors-070.md)。继续保留 Web 目标，
不再以旧版 CAORS 编译失败为理由缩减平台范围。
核心与 libuv 的装配分离、协作输入及平台输入矩阵已完成，输入临时开关已删除。上游修复不等于 Gneiss 浏览器 I/O 或完整 Web Editor 已落地。

CPU 核心分离阶段的构建、消费验证及剩余边界见[验收记录](../records/2026-10-21-caors-core-platform.md)。

协作输入阶段的实现和验证见[验收记录](../records/2026-10-21-caors-cooperative-input.md)。

后续同步入口合并须处理执行器驱动边界：task_executor 不承诺调用方可主动推进，
不能在同步接口内直接等待依赖宿主 run_ready 的请求，也不能嵌套驱动任意外部执行器。
保留同步完成语义与现有公共接口，先明确共享加载过程的驱动方式，再实施合并。
本轮平台验收见[输入统一记录](../records/2026-10-21-caors-input-platform.md)。

## 同步输入准备阶段

同步 Application 入口已转交 asset_manager，取消旧待采用请求后完成有界读取、解析和来源复验。
同步与异步共用输入文档上限及来源比较函数；成功前不修改 InputService，失败保留映射。
不驱动或等待外部任务执行器。该阶段仅统一准备规则，尚未统一 CAORS 缓存发布。

后续仍需让同步与异步共享同一缓存、GUID 与发布代次，并覆盖“旧异步工作未结束时同步加载”
的缓存顺序。不能仅把同步结果塞入缓存，就认为旧请求不会再次覆盖。CAORS 的双 Loader 注册
能力可作为实现基础，但应先验证取消、发布代次和未绑定执行器时的宿主生命周期。

## 资源管理所有权收敛

已删除输入模块的 caors_input_host。CAORS manager、缓存、来源身份与请求发起由
engine::asset_manager 直接负责；输入待采用对象只接收请求，不拥有或访问 manager。
这一步先纠正职责边界，保持原加载、取消和关闭协议。

后续在资产管理层完成同步缓存发布及旧请求顺序协调，不再给输入模块扩充资源管理能力。
材质/Mesh 的 CPU 文档缓存仍需进一步整合，不能把输入状态迁移等同于全部资源统一。

## 已确认的服务与输入配置边界

- Application 拥有资产管理器、输入服务及输入待采用状态，不通过全局 locator 查找它们。
- 项目沿用 input_map URI 引用独立文件；本轮不新增 project.setting.json。
- asset_manager 创建资源请求、维护缓存与来源、推进完成队列，不保存输入采用状态。
- asset_request<T> 是移动所有的薄请求；内部资源及 Runtime 消费者允许使用 CAORS 原生请求与强引用，公共 ABI 和 InputService 保持隔离。
- application/action_map_application 消费原生请求，成功后调用 InputService，不解析文件。
- input_service 只管理映射和输入状态。

当前代码入口：

| 位置 | 职责 |
| --- | --- |
| resource/asset_manager.* | 资源请求发起、缓存与调度 |
| resource/asset_request.hpp | 通用资源请求所有权与结果转换 |
| resource/types/action_map_asset.hpp | 输入资源类型 |
| runtime/loading/action_map_application.* | 请求替换、取消和一次采用 |
| resource/backend/caors/caors_action_map_loader.cpp | 输入类型解析适配 |
| function/input/input_service.* | 输入运行时状态 |

后续同步缓存发布在资产层完成；不得再把“应用到输入服务”的回调放回 asset_manager。

输入资源数据与 Loader 已迁出 Input 功能层，实时动作状态保持独立，见[职责盘点与迁移记录](../records/2026-10-21-cpu-resource-boundaries.md)。

## 同步发布阻碍（2026-10-22）

固定依赖已复现同步重载被旧异步重载覆盖；首次加载没有出现同样回退，取消单个订阅也不足以保证顺序。
尚未把同步输入发布接入 CAORS 缓存。建议先确认上游统一发布资格，避免本地复制代次机制。
复现、最小需求、替代方案及验收见[上游需求](UPSTREAM-051-caors-sync-publication.md)。
