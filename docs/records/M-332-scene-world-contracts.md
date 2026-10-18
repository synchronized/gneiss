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

## 复合值类型补充迁移（2026-10-17）

上一批强类型迁移覆盖主要方法签名及查询，未覆盖全部描述结构。本批补齐：

- Scene 的三种创建描述与 World 候选对象、批量渲染器替换描述采用强类型实体/节点 ID。
  编辑器直接传递这些值；整数拆装仅保留在现有私有存储及适配处。
- Prefab 覆盖键采用 `type_id` / `field_id`，属性类别采用 `property_kind`；
  属性校验通过原生 `registry_ref` 与 `describe_field` 查询，不再接收公共 Registry 句柄。
- 作者属性仍拥有文本与数组，排序、零值规范化、稀疏覆盖、保存恢复规则不变。
  反射回调适配继续显式转换，未更改 C ABI 或场景 JSON。
- 增加描述字段类型编译断言，以及字段查询未冻结、缺失、无效 Registry、成功与跨线程读取验证；
  查询失败保留输出，字段名称仍借用冻结 Registry。

本批 Windows Clang Debug 共享/静态完整构建通过（含公共 C11/C++20 独立头）；
两种配置各 39 项相关回归及 1 项安装 stable runtime 消费者检查通过，共各 40 项。
4 个核心实现的 clang-tidy 修改行无报告告警；修改行格式、UTF-8/LF、`git diff --check` 通过。
静态检查发现属性类别查询的 `std::visit` 异常风险，改为按活动类型返回类别，异常失值返回无效；
属性校验也拒绝该状态。未重跑 Linux、完整 Sponza 或版本级性能验收。
本地日志使用 `build/value-migration-*` 与 `build/value-tidy-*`。
Scene 实例职责进一步内聚及 Application 接口收窄尚未实施。

## 单实例编辑与 Application 依赖（2026-10-18）

- 新增私有 `scene_instance_edit.cpp`，实例维护就绪 Mesh/Material 绑定的捕获、旧版本校验、
  ECS 提交、作者描述和租约更新；服务保留线程、Scene RID 和资产归属校验。
  节点数量与可编辑归属也通过实例查询，不由服务直接检查该容器。
- `scene_mesh_binding.hpp` 和 `render_statistics.hpp` 是独立内部值契约。
  Application 不再为这些签名包含整个 Scene 服务、World、Input 服务或渲染执行器；
  真实使用它们的宿主、ABI 实现和测试显式包含对应接口。
- 纹理服务和内部资产加载器访问方法收为 Application 私有。
  兼容资产入口通过 Application 状态查询、请求及取消进入；旧纹理取消在无服务/无请求时
  保持幂等成功，新测试覆盖线程池及协作模式。
- 头可见性测试同时检查 Application 不间接包含上述具体服务，并拒绝直接访问两个私有加载器入口。

本批 Windows Clang Debug 共享/静态完整构建通过（含公共独立头编译），各 45 项相关检查通过，
包括安装 stable runtime 消费者、Scene/Prefab/编辑器事务、Input、渲染执行器与纹理加载。
4 个核心实现的 clang-tidy 修改行无报告告警；新实例编辑文件全文件检查无报告告警。
修改行格式、UTF-8/LF 和 `git diff --check` 通过。
日志为 `build/ownership-boundary-*` 与 `build/ownership-tidy-*`。
这不是全部 Scene 职责迁移结束：Prefab 批量发布、跨实例资产替换以及其友元访问仍由服务协调；
Application 的资源服务和 Prefab 装配借用入口尚未收窄。本批未执行 Linux、完整 Sponza 或性能对照，
不替代 M-333 的未完成验收。
