<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-159：Asset IPC 结构修订接线

## 结果

Asset IPC 域版本升级为 v2，并新增 `scene` 与 `prefab` 资产类型。协议编码和解码同时执行批次约束：
Texture、Material 和 Static Mesh 可以组成一个渲染事务；Scene 或 Prefab 必须按同类型结构事务发送，
不能与渲染资产混合。

Editor 的待发布状态由单一资产列表改为有序批次队列：

- 同一轮派生输出中的渲染资产合并为一个批次；每个结构资产形成独立批次。
- 每个批次具有独立、单调递增的修订号，同一时刻只允许一个请求等待 Runtime 确认。
- 前一批次成功后继续下一批；失败或需要重启时停止剩余批次。
- 断线重同步先发送渲染资源，再按相同规则发送已知结构资产。

Runtime 在主线程安全点把当前启动 Scene 转交 Scene 事务入口，把 Prefab 转交同源实例批量刷新入口。
非当前 Scene 和当前场景中未实例化的 Prefab 返回成功的无操作结果，避免无关作者资产阻塞运行会话。

## 验证

- Asset IPC 协议测试覆盖 v2、Prefab 往返以及渲染/结构混合请求拒绝。
- Editor–Runtime 进程测试覆盖一次发布同时包含 Material、Mesh 与当前 Scene，并验证拆批后的最终修订。
- Runtime 定向 Smoke、资产协调器和结构重载测试通过。
