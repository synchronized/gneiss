<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-149：首批渲染资产热重载实施记录

## 结果

Runtime 已将 Asset IPC 修订接入 Engine 的真实渲染资产加载链路，首批支持 Texture、Material 和
静态 Mesh。候选缓存按 Texture、Material、Mesh 的依赖顺序构造，任一描述、PNG、Mesh 或后端资源
创建失败时不切换正式缓存。

事务提交后，Runtime 重新绑定当前场景作者节点上的 Mesh Renderer。节点 ID、实体 ID、场景层级和
作者 URI 保持不变，仅资源租约及其 RID 更新。旧租约继续持有旧资源，避免正在使用的对象提前失效；
后端镜像沿用现有 RID 失效清理机制。

Runtime 与 Engine 之间使用私有宿主接口，不把 IPC 消息或 libuv 类型加入公共 Engine API。

## 边界

- 本里程碑不热重载 Scene、Prefab 结构、动画、骨骼或 Morph Target。
- Editor 尚未发布自动导入后的修订，也未展示 Runtime 应用结果；端到端接线和界面反馈由 M-150
  完成。
- 当前刷新对象是活动场景中的作者 Mesh Renderer；Prefab 结构本身不参与本版本热重载。
- 本阶段不需要修改 Granit。

## 验证

- Windows Clang Shared Debug：Runtime IPC、Runtime 冒烟、资源缓存与渲染资产加载共 6 项测试
  通过。
- Windows Clang Static Debug：相同 6 项测试通过。
- 渲染资产测试覆盖乱序输入的依赖排序、三类资源同时替换、Material 引用新 Texture、旧租约保持
  有效，以及候选 Material 损坏时整批回滚。
