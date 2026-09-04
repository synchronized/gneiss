<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-150：Editor 资产热重载接线与验收记录

## 结果

Editor 已在手动导入、手动重新导入和文件监听自动重新导入成功后发布 Asset IPC 修订。运行中的
Runtime 接收增量 `reload`；未连接时 Editor 保留资产快照，并在 Runtime 下次完成握手后发送
`resync`。快速连续导入会合并尚未发送的 URI，修订结果按会话和修订号关联，旧响应不会覆盖新状态。

Asset Browser 展示当前修订号及等待、应用中、已应用、失败或需要重启的 Runtime 结果。非 Texture、
Material、静态 Mesh 产物不会误发到首批热重载事务。

## 验证

- Windows Clang Shared Debug：Asset 协议、Editor IPC 解码、Editor/Runtime 端到端进程、自动重新
  导入、Runtime IPC、Runtime 冒烟、资源缓存和渲染资产加载共 10 项测试通过。
- Windows Clang Static Debug：相同 10 项测试通过。
- 端到端进程测试覆盖运行中应用 Material 与 Mesh 修订，以及 Runtime 重启后的全量重同步。
- 资源专项测试覆盖 Texture、Material、Mesh 的依赖排序、失败回滚和旧租约生命周期。
- [Linux Actions 33849194936](https://github.com/synchronized/gneiss/actions/runs/33849194936)
  的 Clang/GCC Shared/Static、Granit 运行时 Shared/Static 无头测试和 Sanitizer 共 7 个任务通过。

## 人工观察项

- Lantern Gallery 的实际画面变化仍可按需人工观察，不阻塞自动化验收结果。
