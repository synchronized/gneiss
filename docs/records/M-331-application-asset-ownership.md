<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：Application 共享资产所有权迁移

## 范围与结果

2026-10-16，完成 DEV-051 后续三项中的第三项：将缓存及类型加载器从场景域迁出。
当前对象关系以 [总体架构](../concepts/architecture.md#资产与场景域所有权) 为准；
决策依据为 [ADR-064](../decisions/ADR-064-shared-assets-and-scene-instances.md)。

新增 Application 资产装配所有者，活动、候选和退役域只借用。Prefab 历史冻结描述使用同一
加载器。失败/取消候选及旧域退役后回收无使用者缓存项；Application 关闭先排空任务、
销毁场景，再释放资产状态，渲染资源服务最后释放。公共 C/C++ SDK 未变更。

## 验证

本地 Windows Clang Debug，线程池与协作执行均覆盖：

- 两次完整加载同一场景产生独立 World，但实际网格、材质绑定持有同一个版本缓存项，
  Application 逻辑资源字节不因第二个场景重复增长。
- 旧 World 退役后新 World 保持有效；被持有的旧版本租约未被新版本替换。
- 材质 B 候选 ready 和取消均保留 A 默认选择；重试成功激活才采用 B，网格仍共享。
- 已采用 B 后损坏源文件，候选失败并清理，B 默认选择及活动 World 保持有效。
- 既有在途上传取消、排空、关闭、来源变化拒绝和重试回归继续通过。

构建并运行 9 项相关测试全部通过：scene-load-application、scene-load-builder、
editor.prefab-edit、editor.asset-edit、editor.scene-loader、render_asset_loader、
scene_instance、prefab_runtime_instance、scene_prefab_instance。

已对 8 个相关实现/测试翻译单元运行 clang-tidy，退出成功；仍报告既有代码的复杂度、
异常边界及风格告警，未通过关闭规则隐藏告警。格式检查及 `git diff --check` 通过。

## 限制与未验证项

- 本项完成所有权迁移，不等于 M-331 或 0.51 已完整验收。
- 共享版本和后端资源不代表合并并发 CPU 准备任务；重新打开仍复验来源。
- 旧同步兼容获取/重载及 Prefab 描述默认选择尚未全部收敛，不宣称交互入口全部无阻塞。
- 本轮未执行完整 Sponza、真实 GPU 长时间切换、Linux、静态/安装消费者及远端发布矩阵。
  不以这些小夹具结果推断大型场景性能或所有平台的生命周期表现。
