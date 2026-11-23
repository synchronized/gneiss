<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 真实渲染队列压力与关闭验收

日期：2026-11-19。范围：[PLAN-115](../plans/PLAN-115-persistent-render-scene.md)，
Windows Clang Debug 共享/静态本地验证；不是设备丢失或版本完整验收。

## 行为与测试分工

复用 gneiss_asset_pixel_test，增加 --probe-pressure / CTest gneiss.render-pressure，
不新建可执行程序或复制场景装配框架。窗口隐藏，真实 Render 后端执行：

- 等待帧容量 1、命令容量 1，帧输入预算为两个 render::view（本机 336 B）。
- 三视图请求超过预算返回 out_of_memory，原请求保留；减少到两个视图后可以提交。
- 连续提交 32 次必达回读；not_ready 时验证原请求保留，排空后重试。
  校验全部回执身份、顺序、成功且未丢帧，以及 RGBA 输出尺寸。
- 执行完毕帧输入计费归零、峰值不超预算；接受最后一帧后直接 shutdown，
  不预先 finish_frames 或销毁目标。回读完成、目标身份失效、后端存活资源计数为零，
  重复关闭成功。

不将某个固定拒绝次数写入断言：真实 GPU 的调度时机可能变化。
确定性在途容量/队满拒绝仍由现有 render_executor 测试覆盖；
本轮真实 GPU 两种构建均观察到 31 次 not_ready，并全部重试成功。

scene-viewer-dual 复用正式宿主路径验证独立相机、不同目标尺寸、尺寸切换与窗口合成回读。

## 验证结果

共享/静态各通过以下三个测试：

- gneiss.render_executor
- gneiss.render-pressure
- gneiss.scene-viewer-dual

压力用例：两种构建均为 32 次回读、31 次暂时拒绝、336 B 输入峰值、
关闭后存活资源计数 0。最大排队时间分别为 0.071 / 0.072 ms；
这些耗时只作诊断，不设性能门槛。

## 查看器采样

共享 Debug 默认场景，单/双视图各预热 60 帧、采样 300 帧，退出码均为 0。
--measure 除原有宿主耗时 JSON 外，新增独立 render_queue JSON。
原始本地输出位于 build/plan115-pressure/，不作为仓库持久依赖。

| 指标 | 单视图 | 双视图 |
| --- | ---: | ---: |
| 等待任务峰值 | 4 | 7 |
| 命令载荷峰值 | 196608 B | 196608 B |
| 帧输入峰值 | 336 B | 1648 B |
| 命令拒绝 / 替换帧 | 0 / 0 | 0 / 0 |
| 最大排队时间 | 31.133 ms | 42.182 ms |
| 宿主帧间隔中位数 | 8.605 ms | 9.496 ms |

队列峰值包含不同种类任务，不能直接当作等待帧数量。
输入/载荷计费不包含全部场景数据或 GPU 显存；宿主帧间隔不是 GPU 执行时间。

## 结论与限制

当前小场景未出现命令拒绝或替换帧，保留默认 pending_frames=3、commands=12。
字节预算仍为可选限制；缺乏大型场景和多种硬件样本，暂不设置通用默认值。

本次验证的是帧输入压力、真实 GPU 回读和正常设备的关闭回收。
尚未验证上传拥塞、显存耗尽、设备丢失、强制失败路径或跨平台行为；
不据零存活计数声称完成驱动级泄漏检测。PLAN-115 仍未整体完成。

## 2026-11-20：资源发布命令补充验证

继续复用 render-pressure，不新增测试可执行目标。命令载荷预算设置为 4096 B，
提交 8192 B 纹理候选得到 out_of_memory，已接受内容保持原指针。
连续提交 32 个独立的 4096 B 候选；not_ready 时检查旧内容不变，排空后重试同一候选，
成功后检查稳定 RID 已接受该候选。完成后载荷计费归零、峰值不超预算。
原有帧压力与带帧关闭仍执行，并额外检查关闭后纹理 RID 失效。

共享/静态 Windows Clang Debug 相关构建及 gneiss.render-pressure 均通过。
静态本次观察到资源命令拒绝 28 次，全部重试成功；载荷峰值 4096 B，关闭后后端存活计数 0。
拒绝次数随调度变化，只作诊断，不作为断言。

这验证的是 CPU 内容有序发布与命令队列背压。纹理未被绘制采样，不能据此声称覆盖真实
GPU 上传拥塞、纹理像素正确性或显存压力；这些项目及设备丢失、代表性预算测量仍待验收。

## 2026-11-20：纹理上传与像素可见性

新增同一验收程序的 --probe-uploads 入口，对应 gneiss.render-texture-uploads；
不新增可执行目标。使用隐藏窗口和真实后端，命令容量 1、载荷预算 256 KiB。
同一纹理身份依次发布 16 个 256×256 RGBA8 版本，交替纯红/纯蓝。
每个版本通过 UI 四边形采样后回读中心像素，确认新内容真正到达 GPU 绘制；
允许异步上传尚未完成时重取帧，采用 5 秒截止时间，不断言固定就绪帧数。
最后检查命令载荷计费归零、关闭后纹理身份失效、后端存活计数归零。

Windows Clang Debug 共享/静态相关构建和该用例均通过；两次均为 16 个版本、32 次回读、
关闭后存活计数 0。此次补齐连续内容替换后的真实上传与像素可见性，
不等同于高并发上传饱和、显存耗尽或设备丢失验证，不据此修改默认预算。

## 2026-11-20：连续发布与绘制背压

`gneiss.render-texture-uploads` 在原有 16 个逐版本像素检查之后，增加 32 个连续发布版本；
前 31 个提交可丢弃绘制，仅遇到背压才等待并重试，最后一个使用此前没有的绿色并回读验收。
暂时拒绝时检查已接受内容未变化，同一候选重新提交成功后才继续。

Windows Clang Debug 共享与静态构建均通过：35 次捕获、15 次资源发布拒绝、0 次帧拒绝，
最终绿色可见、命令载荷归零且峰值不超过 256 KiB、关闭后存活资源为零。
拒绝次数依赖调度，不作为固定断言；连续阶段不要求每个中间版本都显示。

本结果覆盖资源发布与绘制交错时的队列背压恢复，不代表显存耗尽或 GPU 上传饱和；
设备失败、代表性大场景预算测量仍未覆盖。

## 2026-11-20：神庙场景诊断基线

Windows Clang Debug 共享构建，使用仓内默认神庙；单、双视图分别预热 60 帧、采样 300 帧。
命令为 `gneiss_scene_viewer --measure` 与 `--measure --dual-view`。
另运行 `gneiss.scene-viewer-dual`，独立相机及两轮目标尺寸切换回读通过。

| 指标 | 单视图 | 双视图 |
| --- | ---: | ---: |
| 宿主更新间隔中位数 / P95 | 9.005 / 10.958 ms | 9.268 / 16.765 ms |
| 命令载荷峰值 | 196608 B | 196608 B |
| 帧输入峰值 | 336 B | 1648 B |
| 命令拒绝 / 替换帧 | 0 / 0 | 0 / 0 |
| 排空后命令 / 帧载荷 | 0 / 0 B | 0 / 0 B |
| GPU 有效样本 / 不可用次数 | 178 / 0 | 5 / 186 |
| 最新有效 GPU 帧时长 | 4.421 ms | 0.823 ms |

GPU 最新单帧值不代表完整视图集合的平均成本；双视图计时可用性明显不足，原因尚未定位，
不能用这两个值判断双视图更快。查看器新增输出计时支持性、有效性及样本计数，避免静默使用无效值。

这些是 Debug 单次功能诊断数据，不是 Release 性能基线。神庙由单位立方体和 128×128 纹理构成，
不代表大型美术场景；因此保留现有默认预算，不据本次结果增加通用字节上限。
`--measure` 不执行尺寸切换或场景重载。此次进程正常退出，但 Application 内部关闭统计未返回给
示例，不能据此宣称神庙关闭后设备存活计数为零；此前直接 Render 服务测试的零计数结论仍有效。

后续验收：双视图 GPU 计时可用性、神庙重载与关闭资源统计、Release 重复采样及更大资产负载。

## 2026-11-20：GPU 计时读取路径复核

Gneiss 原先只在窗口呈现分支读取管线指标，离屏和回读分支提前返回。现统一在整个视图批次
成功提交后读取一次最新已完成样本；序号去重和 not_ready 非错误语义保持不变。
返回的仍是单次管线样本，不能相加或宣称是当前多视图整帧耗时。
共享/静态均通过 scene-viewer-dual、render-pressure、render-texture-uploads 三项 GPU 回归。
共享双视图测量仍只有 5 个有效样本、181 次不可用，说明读取位置修正没有解决样本稀少问题。

### Granit 侧待复现项

只读检查当前锁定源码（归档 SHA-256 前缀 `1b5ee78e20ea`）发现：

- `src/pipeline/render_pipeline_api.cpp` 中 measure 首次重置 8 个查询；阴影索引 0/1
  仅在 graph_desc.shadow 存在时写入。无阴影时仍会提交指标回读。
- `src/pipeline/render_pipeline_metrics.cpp` 的 begin_render_pipeline_metrics_read
  总是请求索引 0 起的全部 8 个查询；在途 operation 未完成时槽位不再接受新计时。
- Gneiss 双离屏视图后的窗口合成不提供场景相机和阴影光源。

推断：未写入的阴影查询可能使回读持续等待，逐步占住槽位。这是源码推断，尚未以 Granit
独立测试或查询状态跟踪确认，不将它写成已经修复的根因。

建议由用户向 Granit 发起最小修复：保证可选通道缺省时指标回读也能完成，可为跳过阶段写入
合法的零耗时查询对，或仅读取已写入的查询并将缺省阶段时长设为零。无需增加 Gneiss 专用 API。
验收应连续交替渲染有阴影、无阴影和空场景，覆盖离屏与交换链，检查样本序号持续增长、
缺省阴影耗时为零、关闭无悬挂操作；不要求每帧立即获得结果。
用户完成并更新依赖后，Gneiss 重跑双视图采样与现有 GPU 回归；再考虑完整多视图计时统计。
本轮未修改 Granit，也没有通过禁用计时或强制 GPU 等待掩盖问题。

## 2026-11-20：神庙重载与关闭验收

扩展现有 scene-viewer-dual，不新增测试程序或宿主接口。第一轮绘制完成后释放目标、卸载场景，
检查旧 scene_instance 失效，清空旧相机身份，调用 assets.reload<scene> 并切回 loading。
正常加载流程等待视觉依赖后重新实例化、取得主相机，第二轮创建不同尺寸目标并再次双视图回读。
World 和渲染绑定在重载期间保留，场景对象的删除和新增走原有同步链路。

Windows Clang Debug 共享与静态 scene-viewer-dual 均通过，覆盖两轮独立相机画面、尺寸切换、
旧场景身份失效、异步重载及最终关闭。

更正前述关闭验收限制：Application 虽未向示例返回 device_stats，但 Granit 适配器的
shutdown_gpu 已在 total_live_count 非零时返回 invalid_state，查询统计失败也返回错误。
Render service、Application::shutdown 和 run 逐层传播失败。因此本用例正常退出已包含
后端逻辑资源零存活检查，无需为验收另建接口。这不等价于操作系统显存或驱动缓存完全释放。

本轮未覆盖加载失败恢复、多次循环重载或设备丢失。GPU 计时可用性和代表性预算测量仍未闭环。

## 2026-11-20：后端执行失败的回执与清理

复用 render_executor 现有执行回调，在后端执行阶段返回 dependency_failed；分别验证 required
和 replaceable 帧的错误原样保留、回执身份正确、非丢帧、只执行一次、回执只领取一次、帧载荷归零，
并验证随后提交的清理命令能够完成。没有新增故障注入生产接口。
Windows Clang Debug 共享和静态 render_executor 均通过。

源码复核：Granit 未显式映射的错误归为 dependency_failed，Render 的 collect_completions
对未丢弃帧传播其失败状态。执行器允许清理命令继续运行，不负责设备恢复，也不会自动重试失败帧。
本回归只证明执行器错误交付与清理可达性，不代表真实驱动设备丢失、Render/Application 全链路
故障注入或设备重建已通过验收。上述设备级验收继续保留为待办。

## 2026-11-20：Release 重复诊断采样

MSVC 19.41.34123.0、Release、Intel Core i5-8600K，Granit 提交
`4eebbdd73bca010761b7528ec34bd76ae630d32f`。Gneiss 基于 `45ce384c`，包含本轮
schedule 模板参数 maybe_unused 修正；该修正解决无参数 System 在 MSVC /WX 下的 C4100，
不关闭警告、不改变调度。Release 查看器构建及 scene-viewer-dual 验收通过。

实际渲染 GPU/驱动未从运行时确认，报告明确填写 unverified；以下是诊断基线，不是完整硬件
性能基线。没有用系统枚举的适配器名称代替实际选择结果。

采样脚本修正了旧的一行 JSON 假设，支持 --dual-view，保留全部诊断段并汇总队列数字，
GPU 最新单次样本不参与宿主统计。参数转发、多段合并和汇总检查通过，且用真实查看器验证。
单/双视图顺序运行，各 1 次进程预热、10 次有效采样；每次 60 帧预热、300 帧采样。

| 指标 | 单视图 | 双视图 |
| --- | ---: | ---: |
| 各进程宿主帧间隔中位数的中位值 | 7.698 ms | 9.227 ms |
| 各进程宿主帧间隔中位数范围 | 7.624–7.775 ms | 8.486–9.810 ms |
| 各进程宿主帧间隔 P95 的中位值 | 9.291 ms | 11.680 ms |
| 进程采样 RSS 峰值的中位值 | 147679232 B | 148254720 B |
| 命令 / 帧载荷峰值 | 196608 / 336 B | 196608 / 1648 B |
| 命令拒绝 / 替换帧（每次） | 0 / 0 | 0 / 0 |
| 排空后命令 / 帧载荷（每次） | 0 / 0 B | 0 / 0 B |
| 每进程有效 GPU 计时样本范围 | 177–180 | 5–7 |

原始样本和环境元数据保存在本地 `build/plan115-pressure/release-single.json` 与
`release-dual.json`，不是仓库持久依赖。复现时运行 measure_stable_runtime.py，使用对应
Release 可执行文件、--samples 10 --warmup-runs 1，双视图另加 --dual-view，并填写环境参数。
RSS 为 10 ms 轮询观测到的峰值，不是显存或内存泄漏统计；表中 P95 汇总不等于合并所有帧的 P95。

两组均未达到队列压力，神庙资产规模小，GPU 计时仍不完整，因此维持现有预算默认值。
剩余：实际硬件确认、更大资产及跨硬件样本、Granit 计时问题、真实设备级失败验收。


## 2026-11-20 Granit 0.73.0 接入检查：标准资产不配套

候选依赖为 `9bcf71b54f88d236d06c69a9b7903e1786251123`（远端 v0.73.0），
Windows Clang Debug shared 定向构建成功。升级与阴影投射标志映射仍在工作区，未作为已验收升级提交。

- `granit_version_cache`、`render_scene`、`render-texture-uploads`、`render-pressure` 通过。
- `scene-viewer-dual` 图像验收失败；`asset-pixel` 首次红色纹理等待 5 秒后仍为 R=49、B=156，
  尚未进入热重载阶段。测试在等待期间持续驱动协作式任务和 Application。
- 新版双视图测量取得 146 个有效 GPU 时间样本、0 次 unavailable；这是单次 Debug 诊断结果，
  不是完整多视图 GPU 时间求和，也不用于调整预算。

### 定位证据

比较旧依赖 `4eebbdd73bca010761b7528ec34bd76ae630d32f` 与候选依赖的已提交资产：

1. `assets/generated/installed/libraries/pbr_standard.grshlib` 已改变；新旧各有 16 个 Shader。
2. `assets/generated/installed/materials/pbr_standard.grmat` 字节完全未变。
3. 材质包引用的旧库 16 个 Shader content ID 全部不在新库中。例如顶点 Shader
   `0e4c1261d74e9f036441e4924c3bb736bd4f9c652175a26504916f2e30114ca1`
   在材质包出现 12 次，但新库不再包含它。
4. Granit `material_template_gpu::create_shader` 按材质包的 asset_id 调用
   `granit_shader_create_from_library_content_id`；库中查找失败返回 `GRANIT_ERROR_NOT_READY`。
   固定资产引用不匹配不会随异步加载推进而恢复。

仅在 Gneiss 构建产物中临时恢复旧 Shader 库，保持新版 Granit 二进制和其余加载逻辑不变：
双视图通过，像素测试的初始红色、红转蓝热重载及原有材质状态检查通过，随后停在本轮新增的
关闭投影图像断言。这项对照只定位资产问题，不代表阴影功能通过；已恢复新版 Shader 并重新构建，
不以混用新旧资产作为交付方案。未修改 Granit 仓库或依赖源码。

### 建议交给 Granit 的修复范围

- 以同一批 Shader 输出重新生成标准 Material 包，随 Shader 库一起更新发布快照。
- 增加配套资产验收：标准 Material 的所有非零 Shader content ID 必须能从对应库解析，
  并运行真实网格、纹理和相机的绘制检查，不能只验证资产各自格式合法。
- 标准 PBR 与运行时简化 PBR 分别验证关闭投影、关闭接收，确认正式安装资产覆盖新能力。

这属于 Granit 可复用的标准资产一致性问题，由用户协调上游提交；Gneiss 不修改上游。
修复发布后重新锁定依赖，先复跑上述失败链路和阴影检查，再扩大共享/静态验收。
当前不关闭 PLAN-115，不提交候选升级，不声称像素或阴影验收完成。


## 2026-11-20 Granit 0.73.1 修复接入

远端 v0.73.1 锁定 `e3671a78daf75eaf986dccf3ee45dc4eb24a357d`，源码归档 SHA256 为
`43334ec0eeb15cc82fcc640541c2fe036fadc53a79766c06a1a74351cd02b7d6`。
上游修复 `0b0eab23` 同步标准材质快照及内容哈希；新材质引用新库全部 16 个 Shader ID，
不再引用旧库 ID。此前 0.73.0 的资产配套阻塞已消除，本轮没有修改 Granit 源码。

Gneiss 同步 FETCH/PACKAGE 版本、源码校验和及缓存迁移检查；将 casts_shadows 经帧数据传给
Granit shadow_flags，关闭投射不再导致整个可见实例 unsupported。CPU 检查标志保留，
GPU 检查关闭投射后主视图实例仍然可见。

Windows Clang Debug shared：查看器、像素测试、场景测试构建通过；granit_version_cache、
render_scene、asset-pixel、scene-viewer-dual、render-texture-uploads、render-pressure 全部通过。
其中 asset-pixel 包括初始纹理、热重载、Mip、材质通道和透明/裁剪等原有检查。

独立运行 `gneiss_scene_viewer.exe --measure --dual-view`：60 帧预热、300 帧采样，
198 个有效 GPU 时间样本，0 次 unavailable，末次 Pipeline 样本 1.833 ms；队列拒绝/替换均为 0，
排空后命令/帧载荷均为 0。此数据仍是单次 Debug 诊断，不是多视图合计或预算默认值依据。

### 阴影验收边界

核查上游 `assets/sources/shaders/pbr/pbr_standard.hlsl`，标准材质当前未采样阴影贴图；
相关采样在简化 `pbr_untextured.hlsl` 路径。新增测试原先假定关闭投射必然改变标准材质像素，
这个前提不成立，改为只验证主视图可见性；没有删除或放宽已有图像断言。
也修正了旧 Alpha 用例误称阴影验收的注释：该用例实际比较的是主视图透明/裁剪结果。
真正可见阴影及关闭接收验收仍留在 PLAN-115，不将本次依赖升级等同于阴影功能全部完成。

Windows Clang Debug static：同样三个目标构建及上述六项测试全部通过。
clang-format 检查、git diff --check 通过，本次依赖升级完成本地定向验收。
未覆盖 Linux/Web、安装包 Consumer、真实设备丢失或新一轮 Release 性能矩阵；未执行远端发布。


## 2026-11-20 Granit 0.73.2 标准 PBR 可见阴影接入

锁定远端 v0.73.2 提交 `8eb744482de1dbd380393e0d8a28ae5339e51fb2`，源码归档 SHA256
`20aacb80356b51f12fdbf05ad2c1a7540ba99972bbd61b7ffe2cb07056496ac4`。
上游标准 PBR 新增方向光阴影采样，通过 flat varying 传递接收标志，并重新生成配套材质包。
Gneiss 更新依赖锁定、PACKAGE 最低版本及缓存迁移检查，未修改 Granit 源码。

在现有 asset-pixel 测试中恢复投射开关的像素差异检查，并增加接收面方向性检查：
关闭投射前后蓝色区域仍为接收面，关闭后其中至少一个像素的蓝色通道增加超过 3，
同时中心红色物体仍可见。此检查避免把任意画面差异当作阴影生效，不依赖固定整图黄金文件。
共享构建及 render_scene、asset-pixel、scene-viewer-dual、render-texture-uploads、render-pressure、
granit_version_cache 六项定向测试通过，原有纹理和材质检查未放宽。

独立双视图诊断：60 帧预热、300 帧采样，184 个有效 GPU 样本、0 次 unavailable；
队列拒绝/替换为 0，关闭排空后命令和帧载荷均为 0。仍为单次 Pipeline 时间，非多视图合计，
不据此更改预算默认值。

此前标准 PBR 可见阴影缺口已在 Gneiss 消费链路验证修复。不扩展公开 receive_shadows 接口，
不声称独立关闭接收已由 Gneiss 验收；PLAN-115 的真实设备丢失和跨硬件预算等剩余项不因此关闭。


Windows Clang Debug static 的查看器、像素测试和场景测试构建通过，同样六项定向测试全部通过。
clang-format 与 git diff --check 通过。本轮未执行 Linux/Web、安装包 Consumer 或 Release 性能矩阵，
未推送或发布 Gneiss；结果仅覆盖上述本地共享/静态链路。
