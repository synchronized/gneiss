<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Application 接入 CAORS 输入资源：边界与实施方案

## 状态

2026-10-20，用户已确认方案 A，已接入可选原生 Application 输入加载链路；默认构建、Runtime 和 Editor 调用点尚未切换。
现有闭环见[输入资源应用记录](../records/2026-10-20-caors-input-application.md)。

## 已核实的接入边界

1. `application::load_action_map` 当前同步读取并替换 InputService；公共 C/C++ 接口与
   Runtime 启动流程依赖其完成语义。不能把 success 改成“已提交后台请求”。
2. Application 已有 `asset_file_system_` 挂载表。native 后端验证 URI、解析规范路径，
   并检查结果仍位于资产根目录内；透明来源包装还通过 `complete_validation` 登记摘要。
3. 当前 CAORS 验证后端为空根直接路径后端，不包含上述根目录和来源复验规则。
   因此不能把 asset_root 作为字符串前缀另挂一遍便宣布完成迁移。
4. 已有 `read_source` 有异步范围读与验证回执，现已补充可观察的显式关闭协议（libuv 来源及修订包装支持，其他来源默认 unsupported）。
   CAORS async_file 的关闭结果不能直接以 read_source 析构替代。
5. `GNEISS_ENABLE_CAORS_INPUT` 开启时，输入加载宿主、CAORS manager/cache/Loader
   全部位于 Engine 内部；应用 EXE 不链接独立探针适配库。默认关闭，不改变 C ABI。

源码入口：

- [application](../../src/engine/function/application/application.cpp)
- [Runtime 启动](../../apps/runtime/main.cpp)
- [VFS](../../src/engine/asset/virtual_file_system.hpp)
- [本地后端](../../src/engine/asset/native_file_system.cpp)
- [读取与验证契约](../../src/engine/asset/read_source.hpp)

## 已确认：VFS 迁移顺序

### A：先桥接现有 VFS，接通 Application（已采用）

现有 Gneiss VFS 暂时仍是唯一挂载权威。CAORS 叶后端持有其快照，使用完整资产 URI 转发
异步打开、范围读和来源验证；不建立第二张真实挂载表，也不将 URI 直接转为绝对路径。
CAORS 负责输入资源的请求共享、缓存及句柄；Gneiss 负责来源身份、摘要和应用语义。

先补通用 read_source 的关闭可观察性，再实现桥接。桥接代码放在资产层，不能让平台
CAORS/libuv 适配反向依赖 Gneiss 资产格式。迁移结束后删除桥，不作为长期双 VFS 设计。

优点：首先完成一个真实宿主资源链路，保留已有路径与来源约束，回归范围可控。
代价：存在明确待删除的兼容桥；后续统一 VFS 时仍需迁移底层路由权威。

### B：先将挂载权威统一到 CAORS，再接 Application

由 CAORS 管理唯一挂载表，Gneiss 原 VFS 变为转发接口。先迁移 native 根目录规则、来源身份、
摘要复验和读取回执，再接入输入资源。旧接口保留期间不得双写挂载。

优点：后续资产迁移方向一致，避免先做反向兼容桥。
代价：触及 Prefab、纹理、场景准备、来源修订和资源版本缓存；本轮范围从输入资源接入扩大为
VFS 基础迁移，必须先完成更广回归，不能只靠当前单资源测试放行。

两种方案都禁止以现有直接路径验证后端绕过生产规则。

## 两种方案共用的宿主接入步骤

1. 保留同步 `load_action_map` 的完成语义；先新增原生 C++ 异步 begin/status/cancel 入口，
   不在同一改动中扩张公共 C ABI。未启用可选后端时明确返回 unsupported。
2. Application 在 attach_task_executor 后使用已有调度器及 I/O loop，创建专属任务 scope；
   不另建线程池。资产来源可用前拒绝新请求；重复 attach 不重建在途状态。
3. 将输入资源管理、Loader 与待应用对象作为同一内部子系统拥有，所有 CAORS 对象留在
   引擎模块内。CMake 拆分共享实现源，避免同时链接两个 input_service 实现或两个 CAORS runtime。
4. 主循环在游戏 update 前推进完成与应用；加载失败记录结果并保留旧映射，最新请求优先。
   加载中不阻塞窗口事件。Runtime 首次加载必须等映射可用后才启动依赖它的游戏逻辑。
5. 退出顺序：拒绝新请求 → 取消待应用订阅 → 排空工作 scope → 清理 Loader/完成队列 →
   销毁 CAORS 资源及文件 → 最后停止共享 I/O loop。不得把仍被文件持有的 loop 提前 join。
6. 原生接口和生命周期验证通过后，再迁移 Runtime 调用点；Editor 不因该步骤自动切换。

## 验收

- 真实 Application create/attach/run/shutdown；有窗口与无窗口路径分开验证。
- update 能观察异步就绪结果，未就绪期间主循环仍推进；旧 action_id 仅在成功应用时失效。
- 加载中关闭、重复取消、快速切换请求、失败重载、同步与异步调用交错。
- 路径越界、非法 URI、符号链接/规范路径边界，与旧 native 行为一致。
- 来源变化导致复验失败时不发布；挂载快照与来源身份保持一致。
- 默认关闭与启用两种构建；共享/静态引擎及既有 C/C++ consumer 不泄漏 CAORS 依赖。
- 不以单资源测试通过替代真实宿主、资产根目录与关闭验收。

## 实施进度与剩余边界

已建立资产层 VFS 快照桥，保留原始来源身份，转交异步打开和显式关闭；独立测试覆盖
完整读取、摘要建账及来源变化拒绝、路径越界、缺失文件、取消打开、在途读关闭和重复关闭。
桥仅支持不超过 1 MiB 的完整 CPU 文档，部分读取返回 unsupported，避免把局部摘要当完整版本。
摘要登记与结果复制已改为投递给宿主传入的 worker；pump 仅移交读取回执，关闭回执等待
后台处理结束。测试验证工作队列不推进时主线程不会执行摘要，丢弃排队工作也能结束关闭。
完整读取已通过真实 action_map Loader、CAORS manager/cache 和 InputService 应用验证，
该验证不建立额外 CAORS 挂载表。完成建账不等于发布复验，宿主仍须在应用前复验。
本阶段不宣称解决编辑器卡顿；worker 的实际线程由宿主执行器决定。

可选 Application 集成已实现 `begin_action_map` / `action_map_status` / `cancel_action_map`。
attach 后借用既有线程调度器和 I/O loop；主循环在 update 前推进应用，失败保留旧映射。
同步接口仍返回完成结果，同时取消旧异步待应用操作。关闭时先取消并排空专属工作 scope，
再放弃 Loader 回执、销毁资源，最后由 Application 结束共享 I/O loop。

显式请求首次使用 load，已有缓存时 reload，均重新验证来源，不直接应用旧缓存。
最终复验在 worker 上通过既有 VFS 的有界同步 libuv 读取重新计算摘要，随后才发布 CAORS 缓存；
上限 1 MiB，会占用一个工作线程，但不在主线程等 I/O。这是发布前的变化检测，
不保证复验完成后到主线程应用之间文件系统具有跨进程原子快照。
Runtime 和 Editor 调用点仍使用旧接口；窗口验收、真实项目切换尚未完成。

本地 Windows Clang Debug 验收：启用后的共享 Engine 与 13 项相关测试通过，真实 Application
测试重复 10 次通过；启用后的静态 Engine、默认关闭的共享 Engine 均通过原生入口测试。
静态启用包与共享关闭包分别安装到干净前缀，C/C++、application_world、property_inspector
四项 consumer 全部通过。独立桥接测试另覆盖最终复验失败不替换缓存、复验在工作线程执行。
未验证启用后的共享安装包、实际窗口、跨屏、Emscripten、Runtime/Editor 启动切换及性能改善。
