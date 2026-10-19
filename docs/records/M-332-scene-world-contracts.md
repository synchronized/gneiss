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

## 单场景 Prefab 实例事务（2026-10-19）

- 七个查询/事务入口收回私有 `scene_instance_prefab.cpp`：作者信息和绑定捕获、候选准备、
  新实例发布、实例版本批量替换、分步移除准备及移除提交。服务只做线程与 Scene RID 校验后委派。
- 候选友元由服务改为实例；实例维护作者描述、运行时容器和候选状态的一致性。
  Prefab 源资源编辑、共享资产所有权及跨场景资产替换流程未改变。
- 新增缺失 UUID 失败保留输出、移除准备重置完成标志、非空候选拒绝覆盖、错误线程拒绝发布验证；
  既有跨场景拒绝、取消清理、放置和编辑器撤销重做继续通过。

Windows Clang Debug 共享/静态完整构建通过，含公共独立头编译；两种配置各 40 项相关测试通过，
包括安装 stable runtime 消费者。服务修改行及新实现全文件 clang-tidy 无报告告警。
新实例发布方法有一处局部异常分析抑制：分析器不能关联 vector 容量检查和 push_back；
发布前检查两个容器的剩余容量，并用编译断言保证两类元素移动不抛出，保持发布后无分配。
修改行格式、UTF-8/LF 和 `git diff --check` 通过。
日志为本地 `build/prefab-instance-*.log`。
本批未运行 Linux、完整 Sponza 或性能对照；跨场景协调和 M-333 剩余验收未完成。

## Scene 独占 World 基础拥有链（2026-10-19）

用户确认加载与激活分离。首个代码增量增加私有 scene_instance::create_owned，
由实例独占 World，析构先清理场景对象，再释放 World 并撤销借用 RID；非空输出拒绝覆盖。
原借用构造仅为尚未迁移的服务路径保留，公共加载与 Application 活动域行为本批没有改变。

扩展 scene-load-builder 回归：同一描述创建两个独立 Scene，节点和 World 身份隔离，
Mesh/Material 绑定复用；重复创建拒绝覆盖；跨 World 实体不存活；销毁一份后其 World
句柄失效，另一份仍可访问，最后一份销毁后对应句柄同样失效。
Windows Clang Debug 共享/静态相关目标构建及该测试均通过；实现修改行 clang-tidy 无报告告警，
修改行格式和 git diff --check 通过。日志为 build/scene-owned-*。
未执行完整矩阵、Linux 或 Sponza；公共入口、候选构建及统一资产入口仍待迁移。

## 共享 World 方向修正与资源收集封装（2026-10-19）

用户澄清后确认保留一个 World 多个 Scene。移除上一批仅供测试的 create_owned 入口、
World 所有者成员及其专用测试；原生产加载、异步切换、共享 World 验证保持。
计划和 ADR 同步修正，不再迁移为每 Scene 独占 World。

普通对象及 Prefab 绑定收集收回 Scene 实例；服务只汇总、去重，并在全部成功后替换输出。
跨实例资产替换原子提交尚未进一步封装，不宣称服务私有访问已经全部消除。
新增同 World 重复加载同一 Scene 的回归，验证资源快照去重及分别卸载；既有错误线程
输出保留、跨实例资产版本和 Prefab 覆盖继续通过。
共享/静态相关目标构建及 scene-load-builder 测试各通过；实例资源收集所在文件
clang-tidy 无报告告警，git diff --check 通过。日志为 build/scene-shared-*。
未执行完整矩阵、Linux 和 Sponza；当前收集使用临时引用数组再去重，峰值临时空间随绑定数增长。

## 跨实例资源替换事务封装（2026-10-19）

Scene 实例准备可移动、不可复制的 asset_update，私有记录旧绑定地址和新租约；
服务负责资产类型/归属/重复校验，汇总 ECS 替换描述后只调用一次 World 批量替换。
准备或 World 校验失败时丢弃所有事务，不修改绑定。World 成功后租约移动提交不分配，
用编译断言保证移动赋值不抛出；事务只允许在所属线程同次无重入调用中使用，不跨帧持有。

服务不再遍历普通对象或 Prefab 的私有绑定；移除 prefab_runtime_instance 对服务的友元。
World 共享、固定版本选择、源描述及租约寿命规则不变，Scene 对服务的其他友元尚未清除。

Windows Clang Debug 共享/静态相关目标构建及 scene-load-builder 回归各通过，覆盖错误线程、
外来租约、重复项、普通节点与 Prefab 批量成功及第二个使用者状态不符时第一个不先切换。
实例事务所在文件 clang-tidy 无报告告警，格式及 git diff --check 通过。
日志为 build/scene-update-*；未运行完整版本矩阵、Linux 或 Sponza。
