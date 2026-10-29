<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 资源接口收口验收

## 范围

对应 [PLAN-054](../plans/PLAN-054-render-resource-model.md) 的 2026-10-28 追加范围。
当前契约以 [Render Reference](../reference/render.md) 为准；不覆盖计划中另行启动的 CPU 驱逐、
Mip/LOD 流送或渲染性能扩展阶段，不回写此前 Sponza 与完整测试矩阵的历史结果。

## 实施结果

- 删除 `render_resource_service` 和 `resources()` 表借用入口。Application、Loader、帧构建与 UI
  使用 `render_service` 的 RID 接口；CPU 数据表只保存内容，不保存上层资源对象。
- 将原有弱身份关联、元信息更新、材质强依赖和销毁队列迁入资源层 `resource_bindings`。
  三类资源共用原有强引用控制块与销毁端点，没有新增 URI 缓存、类型擦除基类或 GPU 进度协议。
- 纹理像素视图与销毁队列移到资源目录。程序化纹理强引用通过资源层函数创建。
- 服务构造和 CPU 资源操作不调用 GPU 初始化；未初始化 GPU 时 shutdown 成功并关闭资源，重复关闭成功。
- 私有存储头和身份关联头被限制在装配实现中；宿主可以使用服务接口但不能获取私有表。

## 本地验证

环境：Windows、Clang、Debug、共享库，使用 `windows-clang-debug` preset。

- 完整编译通过，包含 Engine、Runtime、Editor、示例及测试目标；未关闭警告。
- 18 项相关测试全部通过：资源销毁队列、原生渲染生命周期、RID 资源表、材质资源、UI 绘制列表、
  帧包、渲染快照、场景加载、资产准备与 Loader、纹理加载服务与 Application、Prefab 实例、
  场景实例化、Core 正反边界、World 边界、内部头文件可见性。
- 新增/保留验证：工作线程最后释放后不能复活同一 RID 的第二个拥有者；所属线程级联释放材质纹理；
  未启动 GPU 的关闭统计为零；重复关闭及关闭后工作线程晚释放安全。
- 头文件边界验证同时检查旧 `resources()` 入口未恢复，以及私有表和关联实现头不可见。
- 改动行格式检查及 `git diff --check` 通过。

未运行大场景、GPU 图像回归、静态库矩阵、跨平台构建或远端 CI；未修改 Granit。
GPU 上传实现保持原有路径，本次验证不代表真实设备丢失和驱动上传故障已经覆盖。

## 后续职责纠正：资源工厂由 Application 拥有

前述迁移完成了目录和接口收口，但 Render 当时仍拥有 resource_bindings，并通过它更新上层对象。
本次删除该对象，改为 Application 显式拥有 render_resource_factory；Render 不包含、持有或调用工厂。
Loader 主动提交 CPU 内容，成功后由工厂更新资源元信息和显式传入的材质纹理依赖。
销毁队列随工厂关闭，Render 只负责 RID 内容及后端。ImGui 只查询已有纹理引用。
迁移入口仍允许调用方显式接管既有 RID，但材质只能绑定已有纹理引用，不递归创建拥有者。

Windows Clang Debug 全量编译通过，上述 18 项相关测试再次全部通过。
新增验证覆盖 Render 不反向修改上层元信息、缺失/错误材质依赖拒绝且不修改内容、
裸纹理查询不取得所有权、先建立纹理拥有者后材质可重试、工厂关闭不关闭 Render、
重复关闭及跨线程晚释放。Core 负例检查阻止 Render 重新依赖工厂、队列和私有访问器。
改动行格式与 git diff --check 通过。未运行大场景、GPU 图像回归、远端 CI 或跨平台矩阵。

## 后续收紧：删除外部 RID 接管

删除 reference_mesh/texture/material 的成员及自由函数入口；工厂只为自身创建的新 RID 构造拥有者。
Loader 通过 reserve_* 直接取得强引用，程序化资源通过 create_* 创建；材质显式传入纹理强引用。
find_* 仅锁定本工厂已有控制块，裸 RID 和其他工厂的 RID 均不能变成新的拥有者。
队列删除器、创建失败清理及工厂关闭只销毁仍有效的记录，避免重复提交销毁请求。

Windows Clang Debug 全量编译及 18 项相关测试通过，改动行格式检查和 git diff --check 通过。
新增检查覆盖旧接管接口不可调用、资源不可由 RID 直接构造、强引用复制后仅最后释放才回收、
两个工厂共用 Render 时身份与关闭隔离、跨工厂材质依赖拒绝。底层裸 RID 创建/销毁接口仍保留，
上层资源提取的 RID 仅借用，不允许混用其销毁权。
未运行大场景、GPU 图像验证、跨平台构建或远端 CI。

## 工厂迁入功能层

将 render_resource_factory.hpp/.cpp 移到 engine/function/resource，更新全部包含路径、CMake 源清单
和原生接口头清单。删除 Resource 反向依赖功能层的工厂例外；私有访问器授权随工厂路径迁移。
工厂接口和实现除包含路径外保持一致，仍由 Application 独立持有，不并入 render_service。

Windows Clang Debug 全量编译通过；Core 正反边界、World 边界、内部头可见性、Render Loader 与
UI 绘制列表共 6 项测试通过。改动行格式检查和 git diff --check 通过。
未运行大场景、GPU 图像验证、跨平台矩阵或远端 CI。

## 统一资产管理入口

asset_manager 迁到 engine/function/resource，拥有私有资源工厂、类型加载装配和异步批次状态。
Application 删除独立 Factory、Loader 成员；Scene 不再拥有 TextureLoadService，只借用管理器。
运行资源加载文件移出 function/render，位于 function/resource/detail；宿主头清单和边界反例
禁止直接访问这些私有实现。Render 继续仅通过 RID 管理渲染内容，不反向持有资源强引用。
程序化资源创建与 UI 纹理保活改用 application.assets()；原 URI/来源域/类型缓存仍只有一份。

CPU 描述继续使用 load<T>，运行资源批次使用 submit_assets/take，共用同一管理器和文件服务。
本次不增加模板转接队列，不将 CPU 采用成功改为 GPU 就绪。私有解码、依赖准备和销毁实现仍保留。
关闭时拒绝新资源创建与批次，管理器析构关闭资源工厂；残留强引用只保活 CPU 外壳，不再保活 RID。
测试同步更新这一所有权边界，并验证延迟文件读取、缓存复用、纹理依赖、取消和停止后拒绝新请求。

Windows Clang Debug 全量编译通过，29 项相关测试通过，涵盖 CPU 资产管理、运行资源加载、
材质依赖、Scene/Prefab、Application、编辑器资产编辑、销毁队列、模块边界及私有头可见性。
加载测试分别使用协作式和线程池执行器；管理器销毁后晚释放、跨线程最终释放及重复销毁检查通过。
改动行格式化及 git diff --check 通过。

未运行大场景、GPU 图像验证、跨平台/静态库矩阵或远端 CI；未修改 Granit。
当前仍为完整 CPU 负载加载，流送、GPU 驻留控制和统一模板化运行资源请求不属于本轮交付。

## 资产管理目录与私有实现收敛

function/resource 从 16 个文件整理为 13 个：根目录保留管理器、公共加载类型和缓存，
解码及批次实现进入 detail。asset_load_types 合并到 asset_types；
texture_load_service 改为私有 asset_load_batch，完成与状态改名为 asset_load_completion/state。
删除 render_asset_type/reload、prepared_render_asset/batch 的兼容别名；请求使用 asset_type/source。

删除 render_asset_loader 对象和两份文件，其查询与内容采用逻辑直接归入 asset_manager_render.cpp。
管理器不再暴露 prepare_update、apply_update、claim_update 或请求序号分配；
单项更新数据与操作位于私有 asset_render_state.hpp 中，继续拒绝迟到结果，不引入事务。
宿主统一使用 advance、collect_unused、request_stop/stopped 和析构兜底 stop；
删除重复推进、回收、取消和仅停止视觉批次的公共入口。Render 的 RID 与 GPU 管理边界不变。

Windows Clang Debug 编译通过；30 项相关测试通过，覆盖 CPU 缓存、运行资源加载、
材质依赖、请求顺序、取消与停止、跨线程释放、Scene/Prefab、编辑器调用和模块边界。
内部头可见性检查禁止宿主包含解码/更新实现，并在编译期确认管理器不再提供准备和占用更新接口。
改动行格式化、旧符号扫描和 git diff --check 通过。

未运行大场景、GPU 图像验证、跨平台或静态库矩阵、远端 CI；未修改 Granit。
本轮没有增加流送、GPU 就绪等待或统一模板化运行资源请求。

## 类型明确的解码与更新内容

解码结果改为 decoded_asset，content 使用 variant 互斥持有网格、纹理或不可变材质描述引用，
默认 monostate 表示无内容。结果类型从内容推导，不再保存另一份类型标记。
网格、纹理解码与材质描述采用各自使用独立入口；纹理直接使用已解析描述，材质不重复复制参数及 URI。
内部 asset_update 同样使用 variant，材质数据与纹理依赖强引用放在同一个分支。
采用前拒绝空内容、空引用和目标类型不匹配；保留原有请求顺序、缓存及取消语义。

Windows Clang Debug 编译通过，30 项相关测试全部通过。新增检查覆盖默认空内容、空材质引用、
准备与采用阶段的类型不匹配，并确认拒绝后原材质及有效更新仍可使用。
原有依赖加载、输出失败保留、材质引用复用、取消与停止、资源寿命和模块边界检查通过。
旧解码符号扫描及 git diff --check 通过。

未运行大场景、GPU 图像验证、跨平台或静态库矩阵、远端 CI；未修改 Granit。
本轮没有增加动态类型擦除注册、流送或新的加载协议。

## 类型明确的解码输入

asset_load_batch 的 cpu_result 使用私有 decode_input，互斥保存 mesh_decode_input、
texture_decode_input 或 material_decode_input。纹理描述、两段输入字节、设备能力及解析标记
只存在于纹理分支；材质只保存已解码描述引用。CPU 任务通过 visit 分派，各次读取写入对应分支。
公共 URI、来源摘要、预算、诊断和完成状态仍由任务维护，批次推进及取消语义保持原样。
本轮未改造纹理输出布局、整体阶段枚举或文件后端。

Windows Clang Debug 编译通过，11 项相关测试全部通过，覆盖解码准备、加载、材质依赖、
协作式与线程池执行、读取失败、取消后延迟回调、Scene/Prefab 以及 Core/World 边界。
改动行格式化及 git diff --check 通过。未运行独立 clang-tidy、大场景、GPU 图像验证或远端 CI。

## 分离资产加载器与运行资源服务

新增 resource_service，迁入原 asset_manager_render.cpp 中的运行资源创建、身份缓存、
批次装配、内容采用及私有工厂所有权。asset_manager 删除 Render 绑定、运行资源操作和批次接口，
只保留 CPU 类型加载、解码注册及 CPU 缓存。资源服务借用加载器，两者各自拥有不同内容的缓存。
运行资源批次仍由私有 asset_load_batch 编排：材质描述使用 asset_manager.load，
网格和纹理复用加载器的文件服务与来源解析，工作线程调用纯内存编解码器。
本轮不重写类型化加载请求协议，不增加第二份运行资源索引或 GPU 等待。

Application 分别提供 assets() 和 resources()；Scene、运行资源加载、程序化创建及 UI 纹理
保活迁到资源服务。宿主分别推进两者；资源服务停止不关闭共享加载器，关闭后晚释放仍只处理 CPU 外壳。
ADR-087、接口参考、构建清单及私有实现边界同步更新。

Windows Clang Debug 全量编译通过，30 项相关测试通过，包括 CPU 资产、运行资源缓存复用、
混合加载、取消、服务关闭、跨线程最后释放、Scene/Prefab、Application、编辑器和模块边界。
测试确认同 URI 的 CPU 描述与运行资源进入不同缓存，各自回收，并检查资产管理器不再暴露 Render 绑定。
改动行格式化、旧路径扫描及 git diff --check 通过。
未运行独立 clang-tidy、大场景、GPU 图像验证、跨平台/静态库矩阵或远端 CI；未修改 Granit。

## 资源服务内部对象收敛

删除 render_resource_factory.hpp/.cpp 和 asset_render_state.hpp，新增私有 resource_service_impl.hpp。
原工厂的唯一包装索引、销毁队列、创建及更新操作直接归 resource_service::implementation；
服务不再拥有独立工厂对象或第二层工厂 PImpl。原 render_state 合并到 implementation。
asset_update 只保存目标、序号和互斥内容；准备、采用和请求顺序检查由服务内部实现提供。
直接验证内部寿命的测试迁移至私有实现，宿主仍无法包含该实现头。

Windows Clang Debug 编译及 30 项相关测试通过，覆盖 CPU/运行资源、缓存、加载和取消、
跨线程最后释放、关闭后晚释放、Scene/Prefab、Application、编辑器和模块边界。
改动行格式化及 git diff --check 通过。未运行大场景、GPU 图像验证、独立 clang-tidy 或远端 CI。

本轮未统一网格/纹理与通用 load<T> 的加载契约：纹理仍由私有批次编排描述和负载读取，
并向解码器传入设备能力；该剩余工作已在 PLAN-054 中明确，不作为本轮已完成能力。

## 统一 CPU 加载入口

asset_manager 新增 load_mesh_data 和 load_texture_data，返回可移动领取的 asset_data_request<T>。
一次性请求与描述 load<T> 共用文件读取、CPU 执行器作用域、活动操作、取消与关闭流程。
纹理输入通过独立 variant 分支保存描述、负载阶段和设备能力；解析描述后由加载器继续读取负载。
网格和纹理结果不进入长期缓存，不合并独立的一次性请求；运行资源复用仍由 resource_service 管理。
来源快照、字节估算和解码/排队耗时随结果领取。输入失败定位实际读取 URI，负载缺失不再仅报告描述 URI。

asset_load_batch 删除文件请求、任务作用域、CPU 调度和解码流程，仅保存类型化请求，领取 CPU 结果，
安排材质依赖并采用内容。取消丢弃观察者，加载器取消文件和排队任务；正在运行的工作自行保活收尾。
服务关闭不捕获 service 裸指针，也不要求 GPU 上传完成。

Windows Clang Debug 编译通过，30 项相关测试全部通过。新增直接请求验证覆盖网格、纹理两段读取、
移动领取一次、来源快照、无长期 CPU 缓存、资源服务关闭后仍能加载 CPU 内容，以及加载器关闭后的拒绝与晚回调。
协作式和线程池模式均通过；既有预算、加载失败、取消、材质依赖、Scene/Prefab、编辑器和边界测试通过。
改动行格式化、批次直接 I/O/解码调用扫描及 git diff --check 通过。
未运行大场景、GPU 图像验证、独立 clang-tidy、跨平台/静态库矩阵或远端 CI。

## 单资源加载替代基础批次

- 删除 asset_load_batch、asset_update、prepare/apply 和资源更新序号。
- resource_service.load<T> 返回单资源 resource_request；同身份在途请求合并，重载加入既有工作。
  cancel 只释放当前观察权；没有观察者时放弃尚未采用的工作，迟到读取不修改资源。
- 材质通过同一入口请求纹理，依赖失败反馈给材质；成功表示 CPU 内容及依赖已采用，不等待 GPU。
- Runtime resource_load_group 仅组合单资源请求，进度/回执移至 gneiss::runtime。
  results 只统计显式请求；取消保留已成功项；不再施加跨资源的批次字节预算。
- 测试用 prepared_batch 移至测试夹具，生产头不再声明批次准备对象。
- Windows Clang Debug 完整编译通过；30 项相关短测试通过，包括新增的协作式/线程池并发、
  同 URI 复用、观察者取消隔离、依赖失败、缓存、停止及迟到完成覆盖。
  首轮新增夹具缺少材质必填的 base_color_texture:null，修正夹具后通过。
- 未运行大场景、GPU 图像验证、其他平台或远端 CI。加载并发不等于流送或全局内存预算。

## 资源接口与采用路径收敛

- 资源缓存查询统一为 find<T>(uri, output) 与同名混合类型重载；不读取文件，失败清空输出。
- 删除 acquire_* 多套查询接口、mesh_ref/material_ref 包装及 request_input。
  ActionMap 调用通用 load 并显式传 reload=true，原有重新读取与 1 MiB 限制保持不变。
- 网格/纹理从 CPU 请求移动领取后直接采用；材质复用请求已持有的纹理依赖，
  不重新构造 decoded_asset，也不重复查找或登记依赖。
- 删除缓存条目的 unloaded/ready/failed 枚举与 failure 字段，只保留同步创建期间的 is_loading。
  同键递归保护、失败恢复旧值和引用回收仍保留，异步状态继续由请求负责。
- 完整 Windows Clang Debug 编译、33 项相关短测试通过。新增查询不读取文件、命中/未命中、
  类型不符和停止后清空输出、来源身份错误不修改旧纹理，以及回调提前写对象后的递归保护覆盖。
- 未运行大场景、GPU 图像验证、其他平台或远端 CI。三类请求仍按各自所有权语义保留。

## 值结果与错误状态分离（2026-10-29）

- 原状态类迁移到 core/error.hpp，错误码数值和默认 unknown 不变；全仓原生调用改用 error。
  删除状态布尔转换，调用方显式使用 ok()/failed()。
- core/result.hpp 提供 result<T, E = error>，成功值与错误互斥；支持仅移动和不可默认构造的 T。
  result<void> 供泛型使用；轻量状态不携带动态字符串。
- asset_manager 的 load/find_loaded/store 与一次性网格、纹理加载入口直接返回值结果；
  resource_service 的 find/load/create_* 同步迁移 Runtime、Scene、编辑器和测试调用方。
- 内存网格/纹理解码及材质采用返回 result<decoded_asset, asset_diagnostic>，保留详细错误位置。
  不改变缓存、取消、依赖和 CPU/GPU 完成边界。底层就地写入、请求领取及其他领域 out 接口未机械迁移。
- `cmake --build --preset windows-clang-debug -j6` 完整构建通过，43 项相关短测试通过（13.07 秒）。
  覆盖结果类型的成功/失败、仅移动对象与析构、详细诊断，以及资源、文件服务、场景、编辑器和分层检查。
- `git diff --check` 通过。未运行大场景、GPU 图像验证、浏览器/其他平台构建及远端 CI。

当前接口约定见 [Core 值结果](../reference/core.md#错误状态与值结果)与[资产入口](../reference/assets.md#值返回入口)。

## 资源领取与身份入口值返回（2026-10-29）

- asset_request::take() 返回资源强引用或 asset_diagnostic；asset_data_request::take()
  返回 asset_data<T>（内容及同次读取统计）或诊断，不再拆成多个输出参数。
- 等待和失败保留请求；成功消费一次，重复领取返回 invalid_state。返回值独立保活内容。
- resource_service 的公开 reserve_asset/reserve_mesh/reserve_texture/reserve_material，
  以及 asset_manager/resource_service 的 identify 改为值返回；Scene、Runtime 与资源采用调用方同步迁移。
- 新增直接检查空请求、等待、详细失败诊断、成功消费、重复领取与强引用保活的用例；
  一次性网格测试直接领取内容和统计，身份查询覆盖成功及失败。
- `cmake --build --preset windows-clang-debug -j6` 完整构建通过；43 项相关短测试通过（12.27 秒）；
  `git diff --check` 通过。未运行大场景、GPU 图像验证、其他平台构建或远端 CI。
- 私有缓存/Render RID 操作与其他领域的 out 接口仍按原契约保留，本记录不表示全引擎接口迁移完成。

## 缓存与 Render 创建链值返回（2026-10-29）

- resource_cache::load 返回缓存条目的值结果，创建回调返回资源指针或错误；
  保留空对象拒绝、同键递归保护、失败重载恢复旧缓存和引用回收。
- Render 的 reserve/create 系列返回 result<rid>，prepare_*_data 返回不可变 CPU 数据引用的结果。
  取消输出指针及空输出指针分支；destroy/replace 保持已有生命周期语义。
- resource_service 私有 reserve/create 链直接返回资源引用，公开接口无需先用 out 接收再包装。
  RID 已创建但资源包装失败时继续清理 RID 和索引，不改变跨服务拒绝和延迟销毁规则。
- 新增直接值返回的缓存重用/失败重载验证；预留 RID 测试直接验证关闭后的错误结果。
- Windows Clang Debug 完整构建通过。43 项相关短测试首轮 42 项通过；预算测试仍断言旧输出变量被清零，
  改为直接检查失败结果且保留旧资源及预算断言后，该项复测通过。全部相关测试已通过。
- git diff --check 通过；未运行大场景、GPU 图像验证、其他平台或远端 CI。
- 尚未迁移底层 rid_table 的槽位构造接口、帧操作输出及 World/Scene/Meta 等其他领域接口。

## RID 表与帧序号值返回（2026-10-29）

- rid_table::create 直接返回 result<Identifier>，Render 删除临时 allocate_resource 转换函数。
  Scene 实例表调用方同步取值；场景上层尚未迁移的创建接口仍保留自身输出参数。
- 帧准备返回 result<bool>，packet 是原位复用存储；跳过构建与失败是不同结果。
- Render 帧提交、执行器帧提交与命令提交返回 result<uint64_t>，Runtime、初始化、环境更新与关闭调用方同步迁移。
  序号仍对应异步回执；提交成功不代表 GPU 执行完成。
- RID 测试直接验证值返回、类型/所属表/generation 校验、重复销毁、关闭拒绝创建，
  并新增对象构造抛异常后无存活槽位且可继续创建的覆盖。
- Windows Clang Debug 完整构建通过；45 项相关短测试全部通过（12.43 秒），包括 render_executor 与 rid_table；
  git diff --check 通过。未运行大场景、GPU 图像验证、其他平台或远端 CI。
- shutdown 失败时仍需保留资源统计，因此未机械转换为 result<stats>；
  完成队列、World/Scene/Meta 等其他接口尚待继续核查。

## 完成队列值返回（2026-10-29）

执行器帧/命令完成领取及命令状态查询已移除输出参数，使用 optional 值返回。
Render 必需帧领取使用 result<optional<render_frame_completion>>，Runtime 传播收集错误。
保留失败任务完成记录中的序号、进度和统计；新增空执行器和无窗口服务的空结果检查。

验证：Windows Clang Debug 完整构建、45 项相关短测试通过（13.06 秒）；git diff --check 通过。
未运行大场景、GPU 图像验证、浏览器构建或远端 CI。其余领域接口及失败附带统计的接口尚未全部迁移。

## World／Scene／Meta 基础值接口（2026-10-29）

迁移 10 个基础入口：World、Meta、组件注册表的创建；World 实体数量与存活查询；
Scene Tree 创建及两种世界变换查询；Meta 冻结状态与类型数量查询。
所有者工厂不再接收已有变量；World 每次创建独立所属域，所属线程析构与跨线程拒绝不变。
Runtime、编辑器、内置资源注册与属性检查示例同步使用新接口。

新增直接结果断言区分成功的 false／零、未冻结与无效注册表；既有用例继续验证所属线程、
跨 World 句柄、所有者移动、层级变换、反射生命周期及场景加载。
Windows Clang Debug 完整构建、41 项相关短测试通过（11.94 秒），git diff --check 通过。
未运行大场景、GPU 图像验证、浏览器构建或远端 CI。
World 节点／组件值查询、Scene 实例与捕获、Meta 描述／读取／序列化等接口仍待迁移。
