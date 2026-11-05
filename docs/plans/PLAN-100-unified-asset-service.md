<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-100：统一资产服务与场景加载边界

状态：方案已确认，实施待开始。日期：2026-11-05。
归属：[DEV-052](DEV-052-architecture-cleanup.md)。决策见
[ADR-106](../decisions/ADR-106-unified-asset-service.md)。本文中的目标接口和目录均尚未实现。

## 目标与非目标

对外只保留一个 asset_manager，统一 Scene、Prefab、渲染资产及其他已注册资产的加载、
程序化创建、依赖装配、弱缓存和请求生命周期。file_service 提供字节访问，render_service
管理 RID 与后端对象；场景实例管理负责把资产变成 World 中的对象。

不新增外观 Service 包装两套旧管理器；不强制资产继承公共基类；不引入任意插件框架、
资源版本发布协议、依赖事务或流送系统。不重写文件后端、渲染后端、World/ECS，
不同时执行整个 scene/world 目录重排。当前已注册资产全部迁移；未来资产类型使用同一入口。

## 现状与问题

| 当前位置 | 当前职责 | 目标处理 |
| --- | --- | --- |
| function/resource/asset_manager.* | CPU 请求、I/O 调度、解码、弱缓存 | 成为唯一资产拥有者；读取与解码实现收为私有步骤 |
| function/resource/resource_service.*、resource_service_loading.cpp | 渲染资源创建、缓存、材质依赖、RID 衔接、释放 | 并入 asset_manager，最终删除旧类及其独立推进和关闭入口 |
| asset_request.hpp、resource_request.hpp | 两种公开请求协议 | 统一类型化 asset_request<T>，保留内部所需的类型擦除状态 |
| resource_cache.* | 两个管理器分别实例化弱索引 | 一份最终资产索引；不把每个解码中间产物默认登记成公开资产 |
| function/scene/scene_preparation.* | 描述请求、Prefab 依赖、层级排序、实例化输入 | 加载编排归资产服务；纯层级算法复用既有实现 |
| runtime/loading/scene_service.* | 资源加载与场景激活、切换、退场混合 | 移出通用资产加载，保留真正的活动场景生命周期 |
| runtime/loading/resource_load_group* | 渲染资源成组请求与回执 | 迁到统一请求上；仅在调用方确实需要聚合进度时保留薄聚合 |
| resource/*_resource | 稳定运行资源引用、轻量元数据及依赖 | 保留对象模型，不引入第二套资产包装 |

当前 cache() 暴露索引，resource_service.configure_loading 又配置 asset_manager 的执行器。
这两个入口均应收回：宿主负责一次性装配，业务调用方只使用加载、查询与创建接口。

## 对象归属与依赖

```text
宿主拥有：file_service、任务执行器、render_service、asset_manager、场景运行协调器

asset_manager
  ├─ 最终资产弱索引、在途请求与观察者
  ├─ 私有读取/解码步骤 → file_service / 执行器
  ├─ 私有资产装配步骤 → render_service（仅渲染资产需要）
  └─ 场景依赖准备 → 已加载描述、依赖强引用与实例化输入

场景运行协调器 → 场景实例管理 → World
  └─ 持有场景准备结果或资产引用，不创建第二套 URI 缓存
```

资产管理器不拥有 World、节点、编辑器 Document 或撤销历史。渲染服务不持有
Scene/Prefab 描述或资产 URI 缓存。节点和组件的生命周期仍由 World/场景实例管理负责。
无渲染服务时允许加载描述、执行工具解码；创建渲染运行资源明确返回不支持/未配置错误，
不隐式初始化 GPU。各接口错误码在 S1 固定并在调用方迁移时统一。

## 目标 API 与加载完成语义

以下为接口形态示意，准确参数在实现中以最小调用需求确定，不保留旧签名兼容层：

```cpp
auto scene_request = assets.load<scene_description>(scene_uri);
auto prefab_request = assets.load<prefab_description>(prefab_uri);
auto texture_request = assets.load<texture_resource>(texture_uri);
auto mesh_request = assets.load<mesh_resource>(mesh_uri);
auto material_request = assets.load<material_resource>(material_uri);

// 成功结果统一为 asset_ref<const T>；pending、失败与成功可区分。
auto loaded = scene_request->take();
auto cached = assets.find_loaded<scene_description>(scene_uri);

// 示例：创建资产不要求先写文件，匿名对象不进入 URI 缓存。
auto texture = assets.create_texture(texture_view);
auto prefab = assets.create<prefab_description>(description);

// 同一管理器上的场景依赖准备操作；结果不包含 World 或运行节点。
auto preparation = assets.prepare_scene(scene_uri);
```

- load<T> 表示 T 本身已完成构造，不表示任意可达 URI 都已加载。
- Scene/Prefab 描述保留可保存的 URI；普通 load 不创建节点，也不强制加载视觉依赖。
- prepare_scene（以及必要的 Prefab 对应入口）复用同一请求状态与资产索引，解析结构依赖、
  校验层级、收集视觉依赖，交付既有 scene_instantiation_data 的收敛版本。
  它是资产服务上的场景准备操作，不是第二个管理器或第二套请求系统。
- 材质运行对象需要纹理引用，故其构造完成包含这些必需依赖的 CPU 装配完成。
- 渲染资产完成表示 CPU 内容已经采用并关联 RID，不等待 GPU 上传或首次渲染。
- 保留现有场景结构激活与视觉资源后续就绪的区分；资产迁移不把所有视觉资源变成激活屏障。
- 程序化创建和文件加载最终使用同一构造/校验步骤；不同输入类型不强行套成万能 create 参数。
- 工具可直接使用 codec 解码原始数据。mesh_data/texture_data 默认是工作负载，
  不为保持旧入口而重新建立一套公开数据缓存；若真实调用必须保活它们，明确返回只读数据引用。

## 缓存、请求与重载规则

1. 最终资产键统一为来源域、规范 URI、最终 C++ 类型和影响产物的完整参数。
   不使用裸 URI 或参数摘要代替完整身份；reload 是操作，不是新资产身份。
2. 缓存只保存弱引用。最终引用必须能保活缓存条目，最后一个使用者释放后缓存可失效。
   在途任务和依赖引用可以保活结果；缓存不得成为隐式强所有者。
3. 同身份在途请求共享实际工作，各观察者独立放弃。最后一个观察者退出且没有依赖使用者时，
   取消可取消的工作；迟到完成不能更新已失效的请求或覆盖较新的显式采用结果。
4. 描述资产返回不可变快照：重载成功更新索引，已持有的旧引用继续有效；不自动修改编辑文档。
5. 渲染资源保持稳定 wrapper/RID 语义，在所属线程采用新内容；失败保留旧可用内容。
   不承诺一组材质/纹理重载的原子一致性，不重建候选版本与跨资源回滚协议。
6. 同 URI 的描述与运行资源可以是不同最终类型；只有两种最终产物确实被请求时才分别登记。
   解码字节和中间数组按任务/Render 实际持有关系释放，不强行复制，也不强行提前释放。
7. 参数差异必须隔离产物；纹理 profile 属于纹理构建配置，不能继续污染所有普通描述的选项。
   暂不增加通用动态配置字典，先支持实际存在的纹理配置及通用 reload 选项。
8. URI 依赖环应返回可定位错误，不能永远等待；不增加目前无需求的依赖图编辑或共享版本事务。

## 线程、释放与关闭

- I/O 仍由文件后端执行，纯 CPU 解码由宿主执行器执行；资产索引、运行对象装配、RID 操作
  在所属线程执行。禁止主线程阻塞等待异步加载，不新增私有线程池。
- 请求结果与 GPU 就绪分别处理。宿主先分发文件完成，再推进资产服务，最后进行场景与渲染阶段；
  资产服务不偷偷推进文件服务或场景服务。
- 保留确有线程约束的资源退休队列：末次引用可在工作线程释放，RID 销毁在所属线程消费。
  CPU 纯数据不为统一形式而全部进入退休队列。
- 关闭顺序：停止新业务请求并放弃场景/编辑器任务，停止资产请求并继续分发收尾，
  释放 World/实例持有的资源、消费退休队列，然后关闭 Render、执行器和文件后端。
  实际顺序须根据借用关系验证，所有被借用服务必须覆盖其最后一次回调和销毁。
- 明确外部资产引用超出服务寿命时的行为；至少保证其随后析构不访问已销毁 Render。
  停止、重复停止、迟到完成与外部引用释放都纳入验收。

## 目标文件组织

```text
engine/function/resource/
  asset_manager.hpp / .cpp       # 唯一入口与拥有者
  asset_request.hpp             # 类型化请求
  asset_loading.hpp             # 通用状态、统计及必要选项
  detail/
    asset_decode.*              # 私有解码步骤
    asset_loader.hpp            # 现有内部加载步骤，按实际需要收敛
    asset_cache.*               # 私有最终资产索引，由现有 resource_cache 迁入
    asset_construction.*        # 渲染对象装配及释放衔接，按实际体量组织
    scene_loading.*             # 场景/Prefab 描述与依赖准备
```

这是职责示意，不要求每一行新增一对文件；能留在 cpp 的实现不单独建类。
resource/ 下继续保留描述、资源对象及 codec；scene/ 下保留实例、Prefab 绑定和层级算法。
render_resource_ref 等现有类型擦除辅助仅在运行选择类型的调用点保留，不扩散为必需基类。
scene_service 是否最终保留类名由迁移后剩余职责决定：真正负责活动场景和退场则保留；
若只剩转发则并入已有实例/运行协调拥有者，不为删名字把生命周期塞进 Application。

## 实施阶段与退出条件

### S1：固定契约和迁移清单

- [ ] 核查所有 load/find/create/cache/advance/stop 调用方及资源持有关系，形成逐项删除清单。
- [ ] 固定类型化请求、描述/运行资源重载差异、无 Render 错误及场景准备结果。
- [ ] 补最小契约测试；不先重命名全部文件。

退出：每个旧公开入口均有目标去处；不存在未决定的资产所有权。

### S2：合并拥有者与渲染资产闭环

- [ ] 将 resource_service 的装配、运行索引、在途请求和退休队列并入 asset_manager。
- [ ] Texture、Mesh、Material 使用统一请求；材质依赖复用同一入口。
- [ ] 合并最终资产弱索引；移除中间数据的默认公开缓存及可写 cache() 暴露。
- [ ] 宿主一次性配置依赖，移除服务之间反向配置与重复 advance/stop。

退出：三个渲染类型能加载、创建、复用、重载和释放；新增路径不经过独立 resource_service。
迁移期间必要适配只留在内部，同一阶段结束前删除，不能成为长期双轨。

### S3：迁移 Scene/Prefab 依赖准备

- [ ] 将 scene_description_request 和通用资源准备请求迁入资产服务。
- [ ] prepare_scene/Prefab 复用统一加载状态和引用，不保存第二份 URI 索引。
- [ ] 场景运行协调器消费准备结果，保留同步实例化、结构激活与视觉后续加载规则。
- [ ] 场景取消只放弃自己的观察权，不取消其他实例仍等待的共享资产。

退出：同一个 Prefab 加载一次可实例化多次；卸载一个实例不影响其他实例或共享资产。

### S4：调用端迁移与删除旧体系

- [ ] 迁移 Runtime、Scene、编辑器、程序化创建、工具及测试；替换旧资源请求协议。
- [ ] 删除 resource_service、resource_request、旧缓存访问器及无业务职责的转发。
- [ ] 审查 resource_load_group、scene_service 剩余状态，仅保留聚合进度或场景生命周期所需部分。
- [ ] 更新 CMake 内部头清单、当前 Reference/Guide；历史文档仅添加必要的后续决策链接。

退出：源码不再依赖旧管理器；不存在“新的管理器包装旧的两个管理器”的结构。

### S5：统一验收

- [ ] 编译与分层/内部头可见性检查通过。
- [ ] 单资源、依赖、场景实例化与编辑器最小回归通过，记录未验证项。
- [ ] 开发计划、架构决策和当前接口文档一致，提交完整连贯改动。

迁移过程中优先编译与阶段最小验证；架构收敛后统一运行下表回归。
不运行大型场景、耗时 CI 或远端发布；本计划不包含远端及 Granit 跨仓库操作。

## 验收矩阵

| 范围 | 必须验证 |
| --- | --- |
| 统一入口 | Scene、Prefab、Texture、Mesh、Material、既有自定义描述均能通过统一协议加载 |
| 身份与缓存 | 相同身份复用，来源域/类型/参数隔离，失效弱引用可重载，匿名创建不污染 URI 缓存 |
| 请求 | 合并、独立取消、最后观察者退出、迟到结果、失败诊断与重复停止 |
| 依赖 | 材质共享纹理、依赖失败、依赖环、一个使用者取消而其他使用者成功 |
| 重载 | 描述旧引用有效，运行资源身份稳定，失败保留旧内容，不承诺跨资产事务 |
| Scene/Prefab | 加载不创建 World；共享模板多次实例化；卸载隔离；结构激活不等待 GPU |
| 编辑器 | 场景打开、放置、材质编辑、保存、撤销/重做及资源刷新不增加强缓存 |
| 生命周期 | 工作线程末次释放、所属线程销毁 RID、关闭时任务未完成、引用晚于服务释放 |
| 工具 | 无 Render 可读写描述；不意外初始化 GPU；保留专用二进制 codec |

## 风险与实施约束

- 合并身份规则可能改变缓存命中率：保留完整参数，不用减少缓存数掩盖跨配置混用。
- 中间数据不再默认缓存可能增加重复解码：先保留真实使用者的共享负载，不提前做驻留预算。
- 场景切换、编辑器历史持有资源时间较长：明确这些强引用，不能为追求立即回收破坏撤销。
- 接口迁移范围较广：在同一功能分支按连贯阶段本地提交，不拆成长期并存的两套实现。
- 当前无须额外拍板的架构选项；精确函数签名和私有文件数量由实现证据确定。
  若需改变场景激活策略、运行资源身份或新增跨仓库能力，再单独说明影响。
