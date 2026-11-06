<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-102：资源对象与统一加载生命周期

## 状态

实施中：已接入共同资产身份、引用持久化原型和材质依赖解析；当前优先统一资产记录与在途操作，最终对象类型及文件加载协议尚未完成。日期：2026-11-06。归属：[DEV-052](DEV-052-architecture-cleanup.md)。
架构决策见 [ADR-107](../decisions/ADR-107-resource-object-loading.md)。以下 API、格式和目录均为目标示意，不代表现有能力。

## 现状与目标

PLAN-100 已合并管理器，PLAN-101 已分离场景结构与视觉完成，但管理器仍暴露描述准备、渲染预留及 RID 查询等职责。
现有引用、缓存和异步文件服务可复用；不再以给管理器增加外观类作为收敛方式。
目标是资源对象为中心：管理器协调身份和加载，类型加载器装配资源，World 实例化场景，渲染服务管理 GPU。

## 非目标

不实现流送、自动文件监听、资源驱逐预算、跨资产事务、多文件保存事务、通用对象图存档或 GC。
不重写 ECS，不修改 Granit，不顺带实现未知组件或缺失 Prefab 的占位实例化。
不保留旧格式兼容分支；受影响仓库样例和工具必须同步更新。

## 对象与所有权

- 引入薄 asset 基类用于混合资产索引和运行类型检查；复用既有引用控制块，不叠加第二套引用计数。
- texture、mesh、material、scene、prefab 是具体逻辑资产类型，上层持有类型化强引用。
- 不再要求每个类型同时具有 description/data/resource/prepared；确有不同语义的导入设置、像素与顶点负载仍独立。
- 场景资产保存对象树描述和组件属性；运行实例属于 World。一个场景资产可实例化多次，实例 ID 不写回共享资产。
- 资源属性默认只读；修改通过所属线程的显式操作，触发必要更新。工作线程不直接修改已共享对象。
- 同一资产身份的对象稳定；重载先生成局部内容，成功后在所属线程采用。失败保留旧内容，不引入跨资源版本协议。
- 场景资产重载不自动改写所有 World 实例；实例是否更新由场景层明确发起。
- 纹理、网格的大块 CPU 负载允许在后端完成消费后释放；可编辑或需重新编码的资源须保留负载或可重新读取的来源。

## 单一资产记录与迁移主线

最终对象名称表示对象，而不是引用别名；上层使用 `ref<const texture>`、`ref<const mesh>`、
`ref<const material>`、`ref<const scene>`、`ref<const prefab>`。共享资源默认只读，受控修改入口另行定义。

最终内部结构示意：

```cpp
struct asset_entry {
  asset_key key;
  weak_ref<const asset> cached;
  std::shared_ptr<load_operation> loading;
};
```

一个管理器只维护一张按 asset_key 索引的资产记录表；key 包含来源域、URI、目标类型与影响产物的选项。
缓存只弱持有最终资产。loading 强持有当前工作，工作保活构建对象；请求是观察者，取消一个请求不取消其他观察者。
推进用工作列表可以保留，但只是执行队列，不能再作为第二张身份表或扫描去重入口。
完成、全部观察者取消或停止时清空 loading；清理缓存不得删除仍在加载的记录。
显式采用新内容后，旧工作可以向自己的观察者返回结果，但不能覆盖当前记录；以当前 operation 身份校验，
不恢复跨资源版本或事务协议。

落实顺序和独立验收：

- [x] A. 将缓存和 CPU/渲染在途工作归入同一记录表；删除 requests 身份表和渲染 pending 扫描去重。
- [ ] B. texture/mesh/material 改为对象类型，上层统一 ref<const T>；删除同名引用别名及重复描述装配入口。
- [ ] C. scene/prefab 成为 asset 派生类型；共享内容与 World 实例分离。
- [ ] D. 记录 cached 统一为最终资产弱引用；取消 CPU 描述缓存条目、临时 payload 身份键及兼容桥接。
- [ ] E. 类型加载器共用 load_operation 生命周期；覆盖去重、单观察者取消、旧操作迟到、重载失败、清理与关闭。

A 阶段允许缓存槽位临时弱持有 detail::asset_cache_value，以兼容尚未迁移的纯 CPU 描述；
这不满足 D 的最终弱资产引用验收。不得新增另一份永久资产索引绕过上述迁移。
优先完成 A，再继续资源文件格式等支线；完成整个计划要求 A—E 全部通过。

## 职责和目录

| 模块 | 目标职责 |
| --- | --- |
| file_service | 异步字节、路径路由、后端与范围读取 |
| asset_manager | 身份索引、弱缓存、请求去重、类型加载器调度、停止 |
| 类型加载器 | 内存解析、依赖声明与装配、类型校验、完成条件 |
| render_service | RID 与后端资源、提交更新、占位显示、线程内释放 |
| 场景逻辑 | 资产到 World 实例、对象层级、活动场景切换 |

```text
engine/function/asset/
  asset_manager.hpp/.cpp
  asset_request.hpp
  asset_loader.hpp
  detail/
    asset_entry.hpp
    load_operation.hpp
  loaders/
    texture_loader.cpp
    mesh_loader.cpp
    material_loader.cpp
    scene_loader.cpp
engine/resource/
  asset.hpp
  texture.hpp/.cpp
  mesh.hpp/.cpp
  material.hpp/.cpp
  scene.hpp/.cpp
  prefab.hpp/.cpp
  texture/ mesh/ material/ scene/  # 必要负载、编解码与私有辅助
engine/function/render/
  render_service.hpp/.cpp
  detail/...
engine/function/scene/
  scene_instantiation.hpp/.cpp
  ...
```

上述清单是最终布局验收，不只是示意目录。texture/mesh/material/scene/prefab 主头定义对象类型，
上层使用 ref<const T>；不以同名引用别名或转发头冒充迁移完成。资源子目录保留像素、顶点、
二进制格式及私有访问器等有独立语义的辅助文件。

布局迁移验收：

- [x] 管理模块迁至 function/asset；代码、构建、测试、现行指南不再使用 function/resource。
- [x] asset_entry、load_operation 分别定义于 detail 对应头，不再嵌套于缓存类或请求头。
- [ ] 五个具体资产主头位于 resource 顶层；类型名称与上层 ref<const T> 同步迁移。
- [ ] 类型加载器收进 function/asset/loaders；asset_loader.hpp 只定义实际共用的加载契约。
  - [x] 共用字节加载契约抽出，mesh/texture/object 解码实现移入独立 .cpp，三类渲染资源内容采用按类型归档。
  - [ ] Scene/Prefab 加载和依赖装配迁入类型加载器，最终结果统一为资产对象。
- [ ] 旧路径零引用且无转发头；边界检查、整库编译和资源生命周期回归通过。

asset_loader.hpp 已抽出实际共用的字节加载契约，mesh/texture/object 实现进入 loaders，
材质内容采用进入 material_loader.cpp。加载结果仍是 CPU 中间值；依赖装配与最终资产返回契约
尚未统一，不能仅凭文件到位判定最终资产加载器迁移完成。
Scene/Prefab 主头与对象迁移一起实施，不为目录完整创建没有行为的空壳。

加载器放 function 层，避免 resource 数据层反向依赖服务；修正讨论示意中 resource/loaders 的依赖风险。
序列化框架不依赖 asset_manager 或 render_service，类型描述和转换由资源注册提供。

## 目标入口

概念示意，保留现有 result 错误风格：

```cpp
template<class T> result<asset_request<T>> load(asset_uri uri);
template<class T> ref<const T> find(asset_uri uri) const;
template<class T, class... Args> result<ref<const T>> create(Args&&... args);
template<class T> result<asset_request<T>> reload(const ref<const T>& resource);
void collect_unused();
```

load 返回的外层错误表示请求无法提交；请求终态区分可用、带诊断可用、失败及取消。
find 只返回已有逻辑可用对象，不执行加载；未就绪身份通过请求的 resource 访问，不能把非空引用当成加载成功。
request 保存每项依赖的 URI、属性路径、错误码和消息；复制请求共享工作，取消仅放弃当前观察者。
内部仍需 advance/stop 等生命周期能力，但不新增配置组合或多个服务所有者。
删除对外 request_data、prepare_scene/prepare_prefab、渲染 reserve_* 与 RID 反查入口；最后移除迁移桥接。
原始字节直接使用 file_service；程序化资产 create 不强制拥有文件 URI。

## 序列化：对象属性与资源引用

### 保存什么

- 保存注册为持久属性的逻辑数据，使用已有 context-first 序列化入口。
- 不保存指针地址、控制块、RID、请求状态、错误日志、线程对象、GPU 数据和加载缓存。
- JSON 与二进制共享属性及引用语义；各自编码，不把 C++ 内存直接 dump 到磁盘。
- root 保存格式头、版本和内容；目标类型由加载入口/格式确定，不为已知具体类型重复写类型 GUID。
- 内嵌多态对象继续使用已有 $type_guid/$content，普通 variant 使用 $kind/$content；不混用三类身份。

### 引用格式

资源引用按属性类型识别，不以任意字符串外观猜测 URI。目标 JSON 示意：

```json
{
  "format": "gneiss.material",
  "version": 7,
  "parameters": {"color": [1, 1, 1, 1]},
  "base_color_texture": {"$asset": "asset://textures/brick.texture.json"}
}
```

示例版本仅为示意，实施时统一分配实际版本。明确无引用写 null；资源自身的逻辑 nil GUID 规则不变。
ref<texture> 引用不嵌入纹理像素，也不使用多态内嵌对象格式。动态基类资源引用必要时携带类型 GUID并校验实际类型。
同一 URI 多次出现由管理器合并到同一资源对象；保存不递归写其他文件，也不保证跨文件同时保存成功。

### 缺失引用不能变成 null

加载请求取得稳定的资源身份对象，包含来源定位；失败时父资源仍可保留该引用，内容可未就绪。
空引用表示用户未指定；非空但加载失败表示用户指定的资源无法取得。保存后者仍写原 URI。
该身份对象不是失败时冒充成功的内容：请求保留错误，渲染侧检查可用内容后采用占位策略。
匿名程序化资源若被持久化引用，必须先显式保存并赋予来源；否则返回带属性路径的错误，不自动生成文件。
另存为写新文件，不隐式重绑定全部既有共享引用；重新加载新 URI 得到新身份。

### 反序列化与依赖解析

通用序列化遍历字段，不读文件、不等待任务。资源层通过 context 扩展的窄引用解析接口处理 $asset：
解码时规范化定位、取得或预留资源身份、记录目标类型和属性路径；编码时提取 URI。
接口由资产层实现，Core 不直接包含资产服务头文件。精确签名在第一阶段原型中定稿。
加载器在字段解码完成后异步提交依赖，必要时采用依赖内容；不为每种资源重复写字段编解码。
Scene/Prefab 中普通组件仍是可持久化对象值；资源引用用同一个规则，运行 ECS 组件转换单独处理。

## 加载时序与依赖

1. 规范化 URI，形成来源域、路径、目标类型和影响产物的选项组成的 key；同 URI 请求错误类型返回类型错误。
2. 可用弱缓存命中直接返回；在途命中共享 operation；否则建立 entry 和稳定身份。
3. file_service 异步读取，工作线程解析私有临时内容；所属线程负责索引和共享对象变更。
4. 类型加载器收集引用并提交依赖；记录依赖边，检测直接和间接循环。
5. 按类型规定的必要条件完成 CPU 资源装配，在所属线程采用内容、通知观察者。
6. 渲染接入提交 GPU 创建或更新；CPU 完成不等待 GPU 完成。
7. 操作释放临时字节和依赖请求；父资源保留自身真正需要的资源强引用。

缓存仅保存最终资产弱引用；operation 强持有正在构建的结果，结束后释放。解码临时值不形成第二套公开缓存。
取消一个观察者不影响其他观察者和依赖使用者；全部放弃后可停止工作，迟到回调不得访问已销毁管理器。
首版拒绝资源依赖环并给出 URI 链；包括并发根请求之间的环。采用依赖前检测，避免形成强引用环或永久等待。
不通过延迟标记把循环引用偷偷转换成成功。

| 失败位置 | 父资源规则 |
| --- | --- |
| 根文件读取/格式/类型错误 | 当前请求失败；已有内容保持有效 |
| 材质的纹理缺失 | 首次可用默认贴图，带依赖诊断；更新失败保留旧可用内容 |
| 场景引用的视觉资源缺失 | 场景结构可用，视觉独立完成；保留定位和诊断 |
| Prefab 描述缺失、层级非法 | 首版仍阻断对应场景准备，不宣称已有占位实例能力 |
| 循环依赖 | 对应请求报告循环；是否影响父资源按上述必需/可选边界处理 |

必要依赖由类型加载器明确规定，不让所有资源统一承担“依赖全部成功”的条件。
同一批次独立资源继续处理；诊断集合和结果均可供上层读取，不只输出日志。

## 渲染与销毁

渲染资源对象可持有私有 RID 所有权令牌，不保存裸 render_service 借用指针；令牌通过受控释放通道提交销毁。
render_service 不持有资产对象和 URI 缓存；加载器/接入代码提取提交数据，后端只管理 RID 和自身数据。
最后一个资源强引用释放时 CPU 数据正常析构；需要线程归属的 GPU 释放进入渲染释放队列。
先停止新资产请求、处理在途回调、释放 World/资产引用、消费释放队列，再关闭渲染后端。
服务关闭后迟到令牌释放必须安全；GPU 在途使用由渲染后端保证，不以 CPU 引用计数代替 GPU 同步。

## 实施顺序与删除条件

- [ ] 1. 原型：薄 asset、类型化引用、请求结果、资源引用序列化上下文；确定身份与线程契约。
  - [x] texture_resource、mesh_resource、material_resource 共享 asset 身份基类。
  - [x] 混合渲染引用改为 ref<const asset>，类型转换校验进程 type_id 并共享原控制块。
  - [x] 引用位置专用编解码与 context 解析器；纹理、网格、材质通过 `$asset` 往返，预留身份并去重收集依赖，不读取文件。
  - [x] 材质既有描述加载接入引用解析；所属线程预留全部纹理身份，去重提交依赖并保留失败引用。
  - [ ] 请求结果、通用依赖调度与属性路径收集；具体类型重命名及 Scene/Prefab 接入后续阶段。
- [ ] 2. 纹理闭环：文件→资源→渲染，缺失引用往返、弱缓存和销毁；保留二进制专用编解码。
- [ ] 3. 材质与网格：反射字段、依赖诊断及重载；删除重复描述/装配入口，仅保留必要负载类型。
- [ ] 4. Scene/Prefab 作为资产接入统一 load；场景准备迁到场景侧，复用相同引用解析，不新建第二个管理器。
- [ ] 5. 迁移编辑器、导入器、工具、示例和测试；移除旧 request_data、prepare_*、reserve_*、RID 反查及桥接。
- [ ] 6. 更新现行 Reference，检查旧入口零引用、缓存唯一、跨层依赖，完成整体编译和针对性回归。

同一特性分支持续实施；阶段桥接只作为短期实现，不以两套入口并存作为完成。
实施不覆盖用户已有修改；本计划不包含推送或发布。

## 验收

- JSON/二进制持久语义一致；资源引用重复共享、null、缺失、类型不匹配和匿名引用均有覆盖。
- 保存不包含 RID/状态；缺失纹理保存后 URI 不丢失，修复文件后可重试。
- 并发相同请求只读一次；不同选项不错误复用；单观察者取消和停止时迟到回调安全。
- 材质依赖部分失败不阻断其他项；重载失败保持旧内容；循环依赖快速失败并报告路径。
- 同一 scene 资产多次实例化生成不同节点身份；卸载一份实例不销毁其他实例引用的资产。
- 缓存不保活；无资源双重释放；工作线程最后释放和服务关闭后释放安全。
- 每阶段编译及必要回归；完成架构统一后再安排完整平台验证，大场景、流送和远端 CI 不在此次默认范围。

## 风险与待定细节

资源对象反射不能允许绕过更新入口直接修改共享 GPU 相关状态；需定义可写属性和提交边界。
CPU 负载释放与再次保存不能冲突；首版优先保留必要数据，优化驻留留待流送。
薄基类不是通用对象系统；不强迫数学值、组件或任意 C++ 类型继承 asset。
精确类名、context 扩展签名和 Scene 内容采用方式由阶段 1 原型验证，不能以此提前扩展完整插件 ABI。

## 阶段验证记录

2026-11-06：共同身份基类与混合引用接入完成，Windows Clang Debug 完整构建及 8 项资源、场景、
关闭和头文件边界回归通过。新增测试验证类型匹配/拒绝、空引用、控制块共享和最后一次释放。
格式检查与 git diff --check 通过；未运行 GPU 大场景、跨平台或远端 CI。
日志：build/asset-object-build-final.log、build/asset-object-tests.log（不入库）。
资源类型命名、请求终态和真实加载器引用接入尚未迁移，不代表阶段 1 或整个计划已完成。

2026-11-06：资源引用上下文原型完成。通用遍历通过引用位置专用 binding 调用资源 codec，
Core 只依赖窄解析器接口；资产侧预留身份、收集去重依赖，不启动 I/O。纹理、网格、材质的
`$asset` JSON 往返、重复引用共享、空引用、缺失来源身份保留、匿名资源拒绝、非法格式及缺少
解析上下文的属性路径诊断均已验证。Windows Clang Debug 完整构建与 6 项序列化、Scene/Prefab、
资源加载回归通过；未运行大场景、跨平台、远端 CI 或二进制格式验证。
日志：build/asset-reference-build-final.log、build/asset-reference-build-verified.log、
build/asset-reference-tests.log（不入库）。实际类型加载器尚未消费该上下文，依赖异步提交、
成功引用路径收集、全局类型冲突和依赖环检测留待后续闭环，不将原型描述为统一加载已完成。

2026-11-06：材质加载消费引用解析上下文。删除逐槽等待游标，先预留各槽位强引用，再提交全部
唯一依赖；请求观察权与材质持有的引用分开保存。首次纹理失败保留身份，文件恢复后重试更新同一
对象；已有材质重载失败保持旧参数与依赖。新增集成用例覆盖缺失、重复槽位、URI 保存、恢复和
失败重载。完整 Windows Clang Debug 构建及 7 项资源、场景和序列化回归通过，
`git diff --check` 通过。日志：build/asset-dependencies-build.log、build/asset-dependencies-tests.log、
build/asset-dependencies-integration-tests.log（不入库）。未运行大场景、跨平台或远端 CI。
当前仍由工作线程解码 material_description 的 URI 字段，在所属线程解析为引用；材质文件改用
`$asset`、删除旧描述转换及通用依赖环检测尚未完成，不能将这一桥接步骤视为整体迁移完成。

2026-11-06：主线 A 完成。resource_cache 的单一索引现在保存 asset_entry，缓存槽位与当前
load_operation 同属一条记录；CPU operation 和渲染 pending_load 共用操作观察状态。
删除独立 requests 表、CPU 请求 generation 和渲染 pending 身份扫描；推进列表只承担调度。
显式 store 后以当前操作身份拒绝旧工作更新缓存；记录不强持有资产，加载中记录不会被清理。
缓存槽位暂以 optional<weak_ptr<const resource_cache::entry>> 区分尚未缓存和已过期缓存，保持
cached_count 统计口径；这是旧描述/包装层的迁移表示，不是最终 weak_ref<const asset>。
完整 Windows Clang Debug 构建及 8 项资产、材质、场景、销毁回归通过；补充加载中清理、
操作释放及 store 后迟到结果的回归。日志：build/asset-entry-build-final.log、
build/asset-entry-build-verified.log、build/asset-entry-tests.log（不入库）。
未运行大场景、跨平台或远端 CI。下一阶段按 B/C/D 迁移具体对象、Scene/Prefab 和缓存值，
E 的通用类型加载生命周期仍未完成。

2026-11-06：布局迁移首批完成。管理模块从 function/resource 整体迁至 function/asset；
asset_entry 与 load_operation 拆入 detail 对应头，旧缓存包装以 asset_cache_value 命名，
不再混用 entry。纹理、网格、材质主定义及实现迁至 resource/texture、mesh、material 的
顶层同名 .hpp/.cpp；辅助负载和私有访问器仍在领域子目录。同步更新调用方、工具、示例、
测试、CMake 和现行文档，无旧路径转发头。边界检查禁止旧管理目录，并验证新路径下的
私有实现和 CPU 解码隔离。完整 Windows Clang Debug 构建、9 项资源/头文件回归及 2 项
边界正反例通过；git diff --check 通过。日志：build/asset-layout-build.log、
build/asset-layout-tests.log、build/asset-layout-boundary.log（不入库）。
本次不改变资源类型名称：类仍为 *_resource，texture/mesh/material 仍是引用别名；
Scene/Prefab 主对象、最终 asset_loader 契约及 loaders 拆分尚未完成。未运行大场景、跨平台或远端 CI。

2026-11-06：类型加载器拆分首批完成。删除 detail/asset_loader.hpp，抽出 function/asset/asset_loader.hpp
字节加载契约；具体 mesh_loader、texture_loader、object_loader 隐藏在 loaders 对应 .cpp 的私有
命名空间，内部 loader_factory 负责现有类型分派。网格、纹理、材质的内容采用实现分别迁至
mesh_loader.cpp、texture_loader.cpp、material_loader.cpp；材质字段解码仍复用 object_loader。
管理器不再通过头文件编译各类型解码实现。共用操作、观察者、缓存与加载顺序保持原语义。
完整 Windows Clang Debug 构建、11 项资源/生命周期/边界回归、改动文件格式检查和 git diff --check
通过。日志：build/asset-loaders-build.log、build/asset-loaders-build-final.log、
build/asset-loaders-tests.log（不入库）。未运行大场景、跨平台或远端 CI。
本步尚未移除 CPU 中间结果、asset_construction::adopt 入口或材质依赖推进逻辑；
Scene/Prefab Loader、具体资产类型改名与最终 load_operation 契约继续按主线推进。
