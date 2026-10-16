<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：World 候选实体可见性

按 [ADR-065](../decisions/ADR-065-world-publication-groups.md) 实现内部 ECS 发布组。
候选实体可配置组件，但不计入正常实体数，不参与普通查询、系统遍历或渲染快照，也不能成为
活动相机。发布组在所属线程无分配切换，随后正常查询使用原实体 ID；跨 World、重复发布及
发布后继续构造均被拒绝。候选清理由对应组逐实体执行。

Windows Clang Debug 构建与 5 项定向测试通过：world_state、render_snapshot、
world-service-boundary、core-boundary、core-boundary-negative；补充非所属线程发布测试后
world_state 再次通过。覆盖发布前后数量与遍历、相机不被候选替换、取消清理、跨域与旧 ID。
定向 clang-tidy 已运行；既有 World noexcept 路径异常逃逸告警仍在，不宣称静态检查零告警。

该接口尚未用于生产候选场景。Scene Tree、作者描述、编辑器镜像和主相机提交仍需接入统一
边界，不能将当前 ECS 隔离描述为完整 World 事务。组令牌本身不执行取消清理，事务所有者
必须在 World 存活期间释放候选。未做真实窗口、GPU、性能或跨平台验收。
