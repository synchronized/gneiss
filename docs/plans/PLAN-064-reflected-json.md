<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-064：描述类型的反射 JSON 序列化

## 状态

已完成。

## 目标与范围

通过显式 `.serialization()` 启用属性遍历，消除简单描述类型的重复 JSON codec。
自定义 codec 按编码/解码方向优先；不强制全部类型支持序列化。
第一阶段覆盖现有 value 基础类型、独立校验及 Camera / MeshRenderer 迁移。
不实现嵌套对象、容器、流送、Schema 迁移或通用对象继承体系。

## 确认规则

- 以已注册属性名为 JSON 字段；只读属性不输出、不写入，未知字段忽略，不保留在普通 C++ 对象中。
- 缺失普通字段保留默认构造值；required 字段缺失报错，null 不代表缺失；重复 JSON 键拒绝。
- GUID 使用规范小写带连字符文本，沿用属性访问边界，拒绝 nil。
- 字符串沿用属性访问边界，必须为无 NUL 的合法 UTF-8。
- 整数严格校验类别和范围，浮点与数学分量只接受有限值；错误路径使用 JSON Pointer 转义。
- `.validate<Fn>()` 在编码前、解码填充完毕后执行；失败释放临时对象，不暴露半成品。
- 反射工厂仅用于默认构造临时对象；自定义解码继续支持非默认构造类型。
- 所有入口使用调用方的注册表，资源解码适配器持有该表，注册表不反向持有适配器。
- 组件文件字段仍必填，保持 Scene v5 / Prefab v1 现有协议。

## 实施与验收

- [x] 接入统一序列化分派、反射 JSON、Builder 显式开关和校验。
- [x] 组件及 CPU 资产解码统一经过分派，迁移 Camera / MeshRenderer。
- [x] 覆盖往返、默认值、必填、范围、错误路径、只读、自定义优先、隔离注册表、失败释放及并发。
- [x] Debug 完整编译和相关短测试通过；不运行大场景、GPU 和远端 CI。

## 验收记录

2026-10-29：Windows Clang Debug 完整编译及 31 项 Meta、资源、Scene / Prefab 和编辑器短测试通过。
新测试覆盖基础值往返、默认值、必填和只读字段、整数/浮点范围、JSON Pointer、
校验失败释放、自定义 codec 优先与单方向回退、隔离注册表、适配器持有和释放注册表、并发操作。
补齐 int64、uint64、double、string、guid 的稳定类型身份；保留已有基础类型 GUID。
沿用 Meta 属性访问错误语义：getter 输出非法值为 internal，输入属性非法为 invalid_argument。
未运行大场景、GPU、浏览器构建或远端 CI。
