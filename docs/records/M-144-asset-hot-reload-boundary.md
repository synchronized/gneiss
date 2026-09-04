<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-144：资产热重载边界记录

## 结果

已确认 0.22.0 以 Editor 协调的源资产监听和事务式 Runtime 资源替换为核心闭环。现有手动重新导入、
资产索引、VFS、资源缓存与模块化 IPC 分别作为导入、定位、加载、替换和通知基础，不新增含义重叠
的第二套资产系统。

首批支持范围锁定为 Texture、Material 和静态 Mesh。Scene、Prefab 结构、Game Module、脚本与远程
资产传输不进入本版本。完整决策见
[ADR-036：资产热重载使用修订通知与事务替换](../decisions/ADR-036-asset-hot-reload.md)。

## 后续约束

- M-145 的监听器属于 Editor 私有实现，平台事件必须经过内容确认。
- M-146 必须复用现有导入 SDK 和索引事务，失败不能发布新修订。
- M-147 通过独立 Asset 域扩展 IPC，不向 Core 或其他协议域增加资产特判。
- M-148 在真实缓存与 Granit 生命周期上验证候选资源替换；若缺少通用后端能力，先向用户提出
  Granit PR 建议。
