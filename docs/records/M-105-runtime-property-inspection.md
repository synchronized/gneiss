<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-105：Runtime 只读属性检查实施记录

## 结果

M-105 已建立 Runtime 属性检查的首个可用闭环。场景检查快照除节点身份和层级外，还会携带本地
Transform、Camera 参数以及 Mesh Renderer 的 Mesh/Material URI。Editor 可在 Runtime 只读层级中
选择节点，并在 Inspector 中观察对应值；这些控件明确禁用写入，不会修改运行世界或作者场景。

## 类型与字段语义

- Transform 和 Camera 复用现有反射系统的稳定 Type ID 与 Field ID；四元数在界面中转换为只读
  XYZ 欧拉角，协议仍保存无损四元数。
- Mesh Renderer 尚无公共反射类型，因此本阶段仅显示 Mesh 与 Material URI 摘要，不为它创建临时
  Type ID，也不暴露瞬时 RID 或 Granit 后端句柄。
- 选择由 Runtime 会话 ID 与对象 ID（槽位值和 generation）共同约束。会话变化或节点删除后，旧选择
  不会匹配新镜像。

## 边界与验证

协议继续限制单字段字符串长度和总 JSON 负载，拒绝非有限浮点值。快照差异比较包含新增属性，因此
属性变化会产生 upsert 增量，无变化采样仍不发送消息。

已验证 Clang 严格编译下的 IPC 协议、Runtime 快照生成器和 Editor；针对性测试覆盖属性往返、Camera
与资源 URI 的增量变化。大快照分批、显式重同步和每帧应用预算由 M-107 处理，运行统计由 M-106
处理。
