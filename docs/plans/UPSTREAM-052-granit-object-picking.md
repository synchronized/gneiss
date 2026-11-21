<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# UPSTREAM-052：Granit 对象 ID 输出与异步像素拾取

## 状态与用途

2026-11-21 更新：Granit `v0.74.0` 标签已提供主要底层能力，Gneiss 依赖升级与共享/静态定向回归已通过。
本代理只修改 Gneiss，没有修改或提交 Granit。下文保留原需求，实际新增契约见本节对照；
上游能力可用不等于 Gneiss 已实现拾取。
对应 [PLAN-115 的回读与拾取](PLAN-115-persistent-render-scene.md)。
本地升级结果见 [0.74.0 升级验收](../records/2026-11-21-granit-0.74-upgrade.md)。

建议 PR 标题：**RenderPipeline：增加可选的逐 View 对象 ID 输出，复用异步区域回读实现拾取**。

## 0.74.0 实现对照

锁定提交 `ae4d499c68d75d25fc49840c4b797d2a42a783a7`，远端标签 `v0.74.0` 已确认。

- 实际字段为 `granit_render_pipeline_output::object_id_view`，由调用方拥有；
  C++ 对应 `render_pipeline_output::object_id_view`，通过 render_desc.outputs 传入。
- 新增公共 R32_UINT；标准 Opaque/MASK 写入对象 ID，背景清零；BLEND、UI、Debug 不写入。
  object_id 为 0 的物体仍参与深度遮挡。
- 新增 `granit_readback_batch_read_texture_view()`；C++ 为 `readback_batch::read_texture_view()`。
  区域回读保留整数原值，不翻转 Y，复用已有异步操作与结果布局查询。
- 首期不支持 MSAA ID resolve、自定义 record 自动注入及透明拾取策略。
- 上游记录了真实 GPU ID/背景/遮挡及生命周期验收；Gneiss 不将这些记录代替自身拾取验收。
- Gneiss 请求、ID 目标及逐请求身份映射已进入实现验证；关闭验收发现以下上游缺口，Editor 消费待接入，P4 未完成。

## Gneiss 接入发现的释放缺口（2026-11-21）

Granit 0.74.0 的实际单像素拾取已经返回正确对象 ID，但首次启用后关闭 Renderer 前，
Gneiss 检测到 `total_live_count=1`、`graphics_pipeline_count=1`，返回 invalid_state。
纹理、Texture View、Shader 和异步操作计数为零；不是 ID 附件或请求引用未释放。

源码核对（上述锁定提交）：

- `src/pipeline/shadow_draw_recorder.cpp` 的 acquire_object_id_pipeline 创建原生
  graphics pipeline，并保存在 `render_pipeline_state::object_id_pipelines`。
- `src/pipeline/render_pipeline_api.cpp` 的 granit_render_pipeline_destroy 释放
  shadow_pipelines，但未遍历销毁 object_id_pipelines。
- 此缓存保存原生句柄；vector 析构不会代替 granit_graphics_pipeline_destroy。

建议上游最小修复：

1. 在销毁 RenderPipeline 时逐项销毁 object_id_pipelines 并清空，沿用现有首个错误保留规则；
   在释放其依赖的 Shader、Layout 前完成。
2. 审核初始化失败和缓存创建失败路径，避免同类遗漏。
3. 用标准 draw_bindings 执行至少一次 Opaque 和一次 MASK 拾取，覆盖多个缓存变体；
   销毁 Pipeline 及调用方资源后、Renderer 销毁前断言资源统计归零。
   不以 Renderer 最终销毁能清理全部资源代替该检查。

建议 PR 标题：**修复 RenderPipeline 销毁时遗漏 object_id_pipelines 的资源释放**。
该修复属于 Granit 通用生命周期能力，不涉及 Gneiss 的场景、RID 或编辑器协议。
Gneiss 不访问上游私有缓存，也不忽略关闭错误来绕过此问题。

Gneiss 复现命令：

```powershell
cmake --build --preset windows-clang-debug --target gneiss_asset_pixel_test -j1
ctest --preset windows-clang-debug -R '^gneiss.render-picking$' --output-on-failure -j1
```

当前测试保持失败，直至上游修复。GPU 命中、背景、高位 instance_key、旧请求映射、
不同尺寸以及销毁场景后的结果领取已通过前置断言；最后的首次关闭资源归零断言失败。
Editor 点击选择尚未接入，P4 不标记完成。

## 原需求基线与目标

目标是从指定 View 的指定像素取得可见物体编号，供编辑器、模型查看器等上层使用。
Granit 只处理渲染对象编号与纹理，不负责场景树、实体查找、编辑器选择或撤销。

提出需求时核对的 Granit 0.73.2，提交 `8eb744482de1dbd380393e0d8a28ae5339e51fb2`：

| 接口位置 | 0.73.2 能力 | 当时提出的要求 |
| --- | --- | --- |
| `include/granit/pipeline/scene.h` | `granit_scene_renderable` 已有 `uint32_t object_id` 和 `uint64_t payload` | 复用 object_id，不引入上层对象类型 |
| `include/granit/pipeline/render_pipeline.h` | Pipeline 输出描述包含颜色目标、尺寸、Canvas 和 Debug Draw | 增加可选对象 ID 输出；0.73.2 没有公开的拾取附件入口 |
| `include/granit/renderer/readback_batch.h` | 已有纹理区域回读、异步提交、结果布局查询与复制 | 验证或补齐整数附件的单像素读回，不另建一套异步系统 |

字段存在不代表对象 ID 已写入可读纹理；颜色截图也不能代替身份拾取。
本核对只针对上述锁定版本，不断言其他版本缺少相同能力。

## 最小接口范围

### 逐 View 的可选对象 ID 附件

建议扩展 `granit_render_pipeline_output`，由调用方提供可选输出纹理视图。
以下仅为概念字段，最终名称、版本布局与单 View 简写路径由 Granit 确定：

```cpp
// 概念示意：不是当前可编译 API。
granit_texture_view object_id_output;
```

- 使用整数颜色附件，例如 `R32_UINT`；像素值来自 Renderable 的 object_id。
- 每次输出清除为 0，表示背景或没有可返回的对象；供拾取的对象使用非零 ID。
- 每个 View 独立指定附件，使用该 View 的相机、视口、层过滤与可见性规则。
- 不指定附件时不创建拾取附件、不执行额外拾取 Pass；既有颜色输出行为保持不变。
- 调用方拥有附件，Granit 不转移其所有权；在途提交按既有资源退休契约保活。
- 明确格式、尺寸、usage、所属 Renderer 和采样数要求；不支持的组合返回明确错误。
- ID 输出不经过 Tone Mapping、颜色编码、混合或 UI 合成。
- 标准材质/Shader 路径须真正写出 ID。自定义 Shader 如有额外约束必须说明，不能只增加结构字段。

不要求特定实现算法；MRT 或独立 Pass 由 Granit 根据后端和材质体系选择。
必须保证两条路径使用一致的物体变换、剔除与 MASK 判定。

### 第一版命中规则

推荐将范围定义为“不透明及 MASK 物体的可见像素拾取”：

| 情况 | 建议规则 |
| --- | --- |
| 不透明物体重叠 | 返回通过深度测试的最近物体 ID |
| MASK 材质 | 使用正常渲染相同的 alpha 采样及 cutoff，洞口可以命中后方物体 |
| BLEND 材质 | 第一版不参与拾取，也不遮挡拾取，可命中后方不透明物体 |
| 背景、没有命中 | 返回 0；这是成功结果，不是执行错误 |
| 隐藏、层过滤、背面剔除 | 遵循对应 View 的渲染规则 |
| UI、Gizmo、Debug Draw | 不写入场景对象 ID 附件 |
| 可见物体的 object_id 为 0 | 建议仍参与深度遮挡，但返回 0；不把“无 ID”自动解释为穿透 |

透明混合的最终颜色可能来自多个物体，不存在天然唯一的“颜色所属对象”。
BLEND 的上述取舍必须写入契约，不能声称第一版完全匹配所有最终合成像素。
对象 ID 不需要全局唯一；上层保证在本次 View/请求映射范围内无歧义即可。

### 复用现有异步区域回读

- 使用 `granit_readback_batch_read_texture()` 记录 `1×1` 区域，再调用现有异步提交接口。
- 支持读取对象 ID 附件的整数原值，不发生格式归一化或颜色转换。
- 明确回读区域坐标原点、行方向和范围；建议上层契约使用左上角原点。
- 不同后端如有翻转，必须明确由哪个层负责，不能依赖调用者猜测。
- 正确处理 Pipeline 写入到回读之间的执行顺序、资源状态转换与内存可见性。
- 提交后非阻塞查询；成功查询结果布局后复制 ID。越界或格式不支持返回错误。
- 目标销毁、调整大小或重建时，旧请求固定读取原提交的资源，不得读到新目标内容。
- 执行失败与设备关闭均须有明确终态；在途资源安全退休，不留下永久 pending 的操作。
- 复用已有操作取消契约；不要求强行中断已提交 GPU 工作，上层可以只放弃结果。

### 多重采样边界

整数 ID 不允许按颜色平均或线性过滤。
第一版可以明确只支持单采样对象 ID 输出，遇到未支持的 MSAA 组合返回 unsupported。
若同时支持 MSAA，需明确边缘像素选择哪个样本/深度规则，并有测试；不能静默输出无效 ID。
这项取舍由上游确认，不要求为第一版实现完整的多采样拾取策略系统。

## 最小验收

1. 不启用 ID 输出时，既有单/多 View 渲染路径正常，无额外拾取附件和 Pass。
2. 背景为 0；单物体返回其 ID，两个物体交叠返回前方物体。
3. MASK 洞口命中后方物体；BLEND、ID 为 0 的物体遵循已确认规则。
4. 隐藏、层过滤、背面剔除与正常 View 一致；UI、调试绘制不污染 ID。
5. 两个独立 View 使用不同相机和尺寸，各自回读正确；覆盖四角坐标与越界输入。
6. 验证 R32_UINT 的 1×1 异步读回、结果大小/布局，编号不会被颜色转换。
7. 回读在途时销毁目标、调整大小和关闭设备，不悬挂、不提前释放资源，不串读新目标。
8. 单采样或 MSAA 限制按契约验证；支持的后端分别记录验证结果，未测试平台明确列出。

## Gneiss 侧接入

上游能力完成并更新依赖后，Gneiss 再实现完整调用链：

```text
视口点击 → 换算物理像素坐标 → 提交对应 View 的拾取请求
         → Granit 输出/异步回读 ID → 返回请求结果
         → 使用请求保留的身份映射定位 game_object
```

- ID 附件与请求资源由 Render 管理；Editor 负责窗口缩放、视口偏移和坐标换算。
- 请求固定场景身份、目标版本及帧内 `object_id → instance_key` 映射。
  当前 Gneiss 后端使用帧内 `index + 1`，不可使用返回时的最新实例列表解释旧结果。
- World/编辑器绑定层将 instance_key 映射到游戏对象；对象已销毁或场景已切换时丢弃过期结果，
  不得选中复用编号的新对象。
- 独立请求支持 pending、成功未命中、成功命中、取消与执行失败；借鉴现有截图请求的寿命规则，
  不重新公开执行器完成队列，不要求调用方持有共享输出缓冲。
- 结果不能静默改为 CPU 射线求交；后者在 MASK、变形与可见性上可能具有不同语义。

## 非目标与跨仓库边界

第一版不包含框选、多层命中、三角形编号、世界坐标/法线回读或通用选择管理器。
Granit 不接收 Gneiss 的 RID、实体 ID、资产格式或编辑器协议。
本文件可作为上游需求正文，由用户负责提交；Gneiss 仓库仅维护建议与后续接入，
不因此获得修改、推送或发布 Granit 的授权。
