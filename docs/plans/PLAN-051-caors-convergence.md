<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 0.51：CAORS 资源路径收敛

## 目标与状态

用户已确认不长期维护按资源类型开启/关闭的两套路径。目标是 CAORS 成为资源基础依赖，
输入、材质、Mesh 复用统一资源持有协议；同步与异步接口复用实现，仅等待方式不同。
现有三个 GNEISS_ENABLE_CAORS_* 开关尚未删除，本计划不宣称收敛已完成。

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

Gneiss 输入宿主目前仍仅在启动后台 libuv I/O 时装配；VFS 桥要求可轮询读取和显式关闭，
不能仅删除 CMake 的线程限制便认为协作执行器已接通。

## 后续实施

2026-10-21 上游已发布 0.7.0 Web 平台支持，Gneiss 更新依赖并复验原编译障碍。
结果见[升级验收记录](../records/2026-10-21-caors-070.md)。继续保留 Web 目标，
不再以旧版 CAORS 编译失败为理由缩减平台范围。
下一步仍需分离 CAORS 核心与 libuv 适配的 CMake 装配、接入协作输入请求，
再删除临时资源开关。上游修复不等于 Gneiss 浏览器 I/O 或完整 Web Editor 已落地。
