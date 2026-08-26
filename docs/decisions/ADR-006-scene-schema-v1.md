<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-006：场景 Schema v1 持久化契约

- 状态：已接受
- 日期：2026-08-26

## 背景

场景文件需要稳定描述对象身份、层级、Transform、Camera 与 Mesh Renderer，但不能保存运行时实体
ID、Scene Node ID、RID、ECS 布局或第三方类型。加载失败还必须在修改 World 前给出可定位诊断。

## 决策

- 首版格式标识为 `gneiss.scene`，Schema 版本为整数 `1`，建议扩展名为 `.scene.json`。
- 场景与对象身份使用小写规范 UUID 字符串；父对象使用 UUID 或 `null`。
- Transform 明确保存 translation、归一化 quaternion rotation 和非零 scale，不使用隐式默认值。
- components 是显式对象，v1 只允许 Camera 和 Mesh Renderer；资源引用必须是规范 `asset://` URI。
- 同版本拒绝未知字段。新增可选字段需要提升 Schema 版本或先定义兼容规则，避免拼写错误静默生效。
- 解析阶段只产生 Gneiss 内部中间描述，并校验字段、有限数值、UUID 唯一性、父引用、层级循环和
  主相机数量；不创建或修改 World。
- 未来版本返回 `GNEISS_ERROR_UNSUPPORTED`，语法与同版本字段错误返回
  `GNEISS_ERROR_INVALID_ARGUMENT`。诊断保存 JSON 路径；语法错误额外保存字节位置。

## 影响

场景内容不依赖运行时分配顺序或 ECS 实现，可以在提交 World 前完整验证。严格字段策略适合当前早期
格式，可尽早发现错误；格式演进必须显式处理版本，而不能依赖解析器忽略数据。

## 替代方案

- 保存 entity ID、RID 或 EnTT Snapshot：这些值只在单次运行有效，并会绑定内部布局。
- 自动为缺失 Transform 或 components 补默认值：文件更短，但难以区分作者意图和迁移遗漏。
- 忽略未知字段：有利于前向读取，却会掩盖拼写错误，且旧运行时可能静默丢失新组件。
- 首版引入通用反射 Schema：目前只有三个组件，没有足够重复证据支撑稳定抽象。
