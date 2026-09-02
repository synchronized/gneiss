<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-111：Editor Runtime 属性命令状态实施记录

## 结论

M-111 已完成。Editor 现可向独立 Runtime 发送属性写入命令，并在不修改作者场景和命令历史的前提下
跟踪命令结果。Inspector 的具体 Transform 编辑控件将在 M-112 接入。

## 已实现内容

- Editor 与 Runtime 握手时协商 `runtime_property_edit_v1`；旧 Runtime 未提供能力时写入入口返回
  `not ready`，只读检查能力不受影响。
- 独立状态模型按运行时对象、generation、Type ID 与 Field ID 标识属性，每个属性至多一个在途命令。
- Editor 分配会话内单调命令 ID，发送时携带镜像会话 ID 和期望修订号。
- 结果按会话与命令 ID 关联，并保存已应用、被拒绝、超时或断线状态、结果码、诊断、修订号和规范值。
- 两秒未收到结果的命令转为超时；断线、IPC 故障和新会话会终止或清除旧在途状态，不自动重放。
- 迟到、重复和旧会话结果不会改变当前属性状态，也不会进入作者撤销/重做历史。

## 验证结果

- 状态模型测试覆盖单属性在途限制、成功确认、超时、迟到结果、断线和新会话隔离。
- Editor–Runtime 进程测试覆盖真实能力协商、命令发送、重复写入拦截和成功结果关联。
- Windows Clang Shared Debug 全量构建通过，CTest 106/106 通过。
- 公共 C ABI 未变化；新增接口仅属于 Editor 内部 C++ 层。

## 后续边界

M-112 将把该状态模型接入 Runtime Inspector，开放 Transform 平移、欧拉角旋转和缩放编辑，并根据
规范值和后续镜像判断运行逻辑是否覆盖编辑结果。
