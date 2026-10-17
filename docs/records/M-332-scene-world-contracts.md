<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-332：Scene / World 契约收敛

2026-10-17，三项收敛实现与本地验证完成。本记录不代表 0.51 发布验收通过。

## 实现范围

- 共用 `core/scene_types.hpp` 提供节点标识与场景枚举，不依赖 SDK 所有者；原公共入口保留。
- 原生 World 实体/节点操作、Scene 节点操作与查询使用强类型 ID；节点输出使用引用。
  C ABI 保持整数句柄/指针输出，显式复制到相同运行时；保留原失败输出语义。
- `scene_instance.hpp` 是私有实现头；容器与作者描述不再公开，构建器使用只读描述及构造方法。
  Scene 服务继续协调跨实例资源与提交；不改变 Shared Assets 与 World 的生命周期。
- `prefab_candidate` 独占候选/退休运行时与作者描述，支持移动、预览、提交、分步取消。
  Application 不再包含 Prefab 运行时实现；批量提交不再接收作者字段和独占指针的存储地址。
- 宿主可见清单移除 Prefab 运行时头；增加不可见检查及实体/节点类型不可混用编译断言。

## 验证

- Windows Clang Debug 共享/静态完整构建通过，包含公共 C11/C++20 独立头编译。
- 两种配置各 36 项 World、Scene、Prefab、原生 Application、Editor 事务/撤销重做和 Runtime
  查询回归通过；另各 4 项安装、终止行为及接口反例检查通过，合计各 40 项。
- 安装后独立 C/C++ 消费者两种配置各 4 项通过，新场景值头正确安装。
- 新测试验证候选不可复制、无抛出移动、非空候选拒绝覆盖、移动后原对象失效、跨场景提交拒绝、
  未完成候选拒绝发布、错误线程拒绝预览/清理、分步取消及幂等清理，且不新增文件读取。
- 编译验证宿主不可包含 Scene 实例/Prefab 运行时私有头，实体和节点 ID 不能隐式混用。
- 4 个核心实现按仓库规则运行 clang-tidy，修改行无报告告警；修改行格式、UTF-8/LF 和
  `git diff --check` 通过。

回归曾发现私有成员改名误改子树快照 JSON 字段，已修正，相关保存/恢复测试及完整选择集重跑通过。
日志保存在本地 `build/scene-contract-*.log`、`build/scene-tidy-*.log`。

## 边界与限制

内部整数表存储、场景实例/刷新令牌 RID、结果码及真正反射回调仍保留原语义；不是全引擎
所有 C 风格类型清零。没有新插件 API、第二套运行时状态、Granit 修改或性能优化。
未运行 Linux 或完整 Sponza 对照；[M-333 剩余验收](M-333-native-migration-verification.md) 继续有效。
