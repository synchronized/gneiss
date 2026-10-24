<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# RID 与 Service 生命周期

## 内部 RID

内部使用 `gneiss::engine::rid`，是独立定义的 64 位整数资源标识，零表示无效资源。
它不拥有资源，也不提供编译期资源类型隔离。旧公共 RID 头与 C++ 包装均已删除；
游戏模块的 World 等借用标识由模块协议单独定义。

调用方不得解析、持久化或自行构造 RID。当前内部编码包含槽位、generation、资源类型和 Service
domain，但位布局不是公共 ABI，后续版本可以调整内部编码。

## 有效性

Service 在每次使用 RID 时验证：

- RID 非零且槽位存在。
- generation 与当前槽位一致。
- 资源类型与调用接口一致。
- Service domain 与接收调用的 Service 实例一致。

资源销毁后槽位 generation 递增，因此旧 RID 即使遇到槽位复用也不能访问新资源。generation
耗尽的槽位会永久退役，避免历史 RID 再次有效。来自其他 Service 实例的 RID 会因 domain 不匹配
而被拒绝。

无效 RID、类型错误、旧 generation、跨 Service 使用和重复销毁统一返回
`GNEISS_ERROR_INVALID_HANDLE`。创建接口的输出指针为空、Service domain 为零或资源类型无效时返回
`GNEISS_ERROR_INVALID_ARGUMENT`。

## Service 生命周期

当前内部 Service Registry 按显式依赖关系初始化 Service，而不是依赖注册顺序。关闭时按照实际
初始化顺序逆序销毁：

```text
Platform → Resource → Render
Render → Resource → Platform
```

- 缺少依赖或依赖循环返回 `GNEISS_ERROR_DEPENDENCY_FAILED`。
- Service 初始化失败时，失败 Service 和已经初始化的 Service 立即逆序回滚，并保留原始错误码。
- 重复初始化返回 `GNEISS_ERROR_INVALID_STATE`。
- 关闭前统计各 Service 仍存活的资源数量，供后续诊断接口报告。
- 首版 Registry 只由 Application 创建线程访问，不提供内部并发同步。

Service Registry 当前是内部实现，不构成公共 ABI。后续 Application API 会拥有 Registry，并将
初始化失败、资源遗漏和关闭结果转换为公共诊断。
