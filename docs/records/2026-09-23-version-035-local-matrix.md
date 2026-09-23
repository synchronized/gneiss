<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 0.35.0 本地矩阵与 IPC 测试检查

最终候选的发布矩阵及验收范围见 [M-236 发布验收](M-236-0.35.0-release-validation.md)。
下文保留逐轮验证过程与当时状态。

- 日期：2026-09-23
- 范围：M-236 的 Windows 本地验证；不构成发布验收完成。
- 关联：[实施计划](../plans/VER-035-0.35.0-texture-asset-variants.md)、
  [M-235 验收](2026-09-23-texture-variant-diagnostics.md)。

## IPC 测试检查

上轮 `gneiss.runtime.ipc-session` 失败时没有用例诊断。本轮修改前连续执行 30 次未复现，不能将
历史失败唯一归因于某一原因。

源码检查发现辅助函数一次从 Transport 队列移出最多 64 条事件，却在遇到首条 Envelope 后返回，
同批次剩余事件随局部容器销毁。Ready 与属性响应同批到达时，后者可能被测试自身丢弃。现使用
既有 `max_count=1` 参数，每次只消费一条事件。控制生命周期测试同时等待日志与 Pong 到齐，避免
依赖它们的到达顺序；六个子用例均补充失败名称。没有修改 Transport 或 Runtime 协议。

修正后使用 `ctest --repeat until-fail:100` 验证通过，测试超时和断言保持原标准。clang-format 通过；
clang-tidy 仍报告文件原有的聚合初始化形式及 main 异常逃逸告警，未将静态检查描述为全部清零。

## Lantern 暂停检查

MSVC Shared 全量测试再次出现 Lantern 工作流无诊断失败。补充行号、进程状态与暂停计数诊断后，
单独重复测试复现：暂停期间旋转不变，但收到的进度日志数从 1404 增至 1799。日志消费线程仍在补送
暂停前的消息，消息到达数不能作为暂停是否生效的依据。

改用暂停确认后的新统计，检查固定更新计数不变，并要求观察期收到更新的统计序号，避免旧统计造成
假通过。另一次复现显示固定更新计数保持 28 而镜像旋转变化：属性响应和场景快照独立投递，基线取早了。
现等待暂停后的 Scale 编辑在场景镜像中实际出现，再采集旋转基线。恢复后仍检查进度、固定更新和
旋转均继续变化。未扩大用例超时，未改变 Runtime 行为；MSVC 与 Clang 下各连续 10 次通过。

## 验证环境与结果

Windows，Granit 0.28.0，平台、Editor、Runtime 与测试启用；MSVC Release 和 Clang 23.1.1 Debug，
各自使用独立 Shared/Static 构建目录，Gneiss 警告视为错误。

| 配置 | 构建及测试 |
| --- | --- |
| MSVC Shared Release | 139/141 首轮通过；安装超时和 Lantern 失败的处理见下文 |
| MSVC Static Release | 完整构建及 138/138 测试通过，含安装 Runtime 与稳定 Consumer |
| Clang Shared Debug | 完整构建及 141/141 测试通过，含 Cooked Lantern 正常与损坏负载、安装验收 |
| Clang Static Debug | 完整构建及 138/138 测试通过，含安装 Runtime 与稳定 Consumer |

MSVC Shared 安装打包首次超过 120 秒，单独复测在原时限内用时 27.75 秒通过；未确认首次超时的唯一
原因。Lantern 按上节修正并重复验证。Clang Shared 全量结果早于最终 Lantern 断言修正，修改后仅重跑
该受影响用例，不重复完整矩阵。当前已完成的全量测试均包含安装 Runtime 与稳定 Consumer 检查。

## 剩余验收

### Granit 0.29.1 与编辑器修复后的补充验收

本轮以 `65fd712` 为代码基线重新配置并构建 MSVC Release Shared/Static，确认依赖固定到
`462d19b88678ea4048bfdeddbee1ea37955527b6`。两套均启用 Editor、真实 Granit 后端及严格警告。

| 配置 | 结果 |
| --- | --- |
| MSVC Shared Release | 完整构建，142/142 一次通过，81.66 秒 |
| MSVC Static Release | 完整构建，139/139 一次通过，48.50 秒 |

Shared 安装打包 31.09 秒、停止协议 1.17 秒、Cooked Lantern 9.74 秒、Lantern Runtime 工作流
2.90 秒，均在原限制内通过。新增的可选源目录监听回归以及 Editor 告警失败条件包含在本轮测试中。
日志位于本地 `build/035-msvc-shared-tests.log`、`build/035-msvc-static-tests.log`。
Clang Shared/Static 的 0.29.1 全量结果见[依赖升级记录](2026-09-23-granit-0.29.1-upgrade.md)，
其中 Shared 的两项首次失败和复测结果保留，不记为首轮全绿。
资产监听修复后的 Clang Shared 专项结果见 [M-145 补充记录](M-145-asset-file-watcher.md)；本轮
Clang Static 重建 Editor 后，监听、作者监视、导入、重新导入队列及两项 Editor smoke 共 6 项
同样通过，日志为 `build/035-clang-static-watch-tests.log`。未因文档整理重复已通过的完整矩阵。

### 尚未完成

- 本机 `wsl --status` 仍返回 `WSL_E_WSL_OPTIONAL_COMPONENT_REQUIRED`，且无 Docker 命令。
  Linux Clang/GCC 与 Sanitizer 已通过下述远端矩阵，不再属于自动化验收缺口。
- 场景 Y 方向已随 [Granit 0.28.1 接入](2026-09-23-granit-0.28.1-upgrade.md)完成修复与 GPU 读回验证。
- 用户已确认升级 0.29.1 后实际滚轮拉远不再卡住或跳动；程序化两次 resize 后持续呈现通过，
  详见 [0.29.1 验收](2026-09-23-granit-0.29.1-upgrade.md)。真实鼠标 resize、Gizmo 拖动及全部桌面
  交互仍未完成验收，不能用短程 smoke 替代。
- 分支已推送并启动 Linux 验收，结果与修复见下节；尚未合并、打标签或发布。M-236 保持进行中。

## Linux 首轮验收与无后端构建修复

用户同意继续远端验收后，对 `9efe1c5` 触发
[Linux 工作流](https://github.com/synchronized/gneiss/actions/runs/35852910669)。
Sanitizer 任务通过，四个 core 配置均在编译纹理加载器时失败：关闭 Granit 后端的 Engine
无条件包含 `granit/renderer/texture_asset.hpp`，破坏了核心库的可选依赖边界。

修复将 Granit Manifest 检查限定在启用后端的构建中；关闭时加载 `.gneiss-texture` 明确返回
`GNEISS_ERROR_UNSUPPORTED`，保留 `/source` 诊断且不创建 RID。PNG 与 KTX2 路径不变。
独立加载器测试与 Engine 使用相同能力宏，分别检查正常加载和不支持路径。

Windows Clang 无后端 Engine 及加载器测试构建、测试通过，导入表确认 Engine 不依赖 Granit DLL。
该修复没有通过给核心库强制链接 Granit 或关闭失败矩阵来规避问题。启用后端的加载器、Editor
smoke 和 Cooked Lantern 共 3 项本地回归通过；加载器 clang-tidy 仍有原有代码告警，未声称全部清零。

首轮图形 Shared/Static 各 124 项测试通过。修复提交
`2c30f150466a4ff7ba8b58d8b6fbb51c9845a053` 推送后执行
[第二轮 Linux 工作流](https://github.com/synchronized/gneiss/actions/runs/35853733141)，7 个任务全部成功：

| 配置 | 结果 |
| --- | --- |
| Clang Core Shared / Static | 各 73/73 通过 |
| GCC Core Shared / Static | 各 73/73 通过 |
| Clang Granit Runtime Shared / Static | 各 124/124 通过，包含无头窗口测试 |
| GCC Sanitizer Runtime | 设备创建失败检查、Application/Scene 两项测试及图形 smoke 通过 |

Sanitizer 沿用工作流既定边界：Application/Scene 检查启用泄漏检测；图形 smoke 使用 ASan/UBSan，
但按现有配置关闭泄漏检测。该结果不代表所有测试或图形驱动泄漏均经过 Sanitizer 验证。
本轮只触发 Linux 工作流；Windows 结果来自前述本地矩阵。后续纯验收文档提交不重复运行矩阵。

## 场景 Y 方向诊断

后续已接入上游 0.28.1 并验证方向修复，见[接入记录](2026-09-23-granit-0.28.1-upgrade.md)。
以下保留 0.28.0 的诊断与当时建议，不代表最终修复实现。

在 Granit 0.28.0（`1cb4e7456c339b2f4edfdded050ada1587e1b5f5`）和 Clang Shared Debug
下构造不对称场景：相机位于 `(0,0,2)`，朝向保持单位旋转；红色三角形位于 `(-0.55,+0.55,0)`，
蓝色三角形位于 `(-0.55,-0.55,0)`，各缩放至 0.35。三角形添加反向顶点副本，避免背面剔除干扰。
在右侧相同世界 Y 坐标添加红、蓝 Debug Draw 线，关闭线段深度测试。

临时诊断代码在第三帧将同一 Render Pipeline 输出到 1280×720 BGRA8 SRGB 纹理并读回。
每行 5120 字节，直接保存为 PNG，未翻转像素行。结果如下：左侧场景红色物体位于下方、蓝色位于上方，
三角形尖端也朝下；右侧调试线仍为红上蓝下。两条路径的方向不一致已确认，并非截图整体上下翻转。

![场景三角形与同世界 Y 坐标调试线的方向差异](images/2026-09-23-scene-orientation.png)

源码检查定位到以下链路：

- Gneiss `src/render/camera_math.cpp` 的投影使用 `-focal`，世界 +Y 经正高度 Vulkan Viewport
  投影至图像上方；场景与 Debug Draw 使用同一 View Projection。
- Granit `assets/sources/shaders/pipeline/tone_mapping.hlsl` 的全屏三角形使用
  `uv = position * float2(0.5, -0.5) + 0.5`，而 Vulkan 色调映射使用正高度 Viewport，
  因此合成时反向采样 HDR 场景的行。
- Debug Draw 在色调映射之后绘制，没有经过这次采样翻转。单独反转相机不能消除两条路径的差异，
  还需要验证深度与调试图元的一致性。

建议交由用户向 Granit 提交的最小 PR：修正 Vulkan 最终合成的屏幕位置到 HDR UV 映射，并保持
WebGPU 的后端约定；不引入 Gneiss 场景或资产语义，也不新增要求调用方翻转相机的接口。
验收应使用非对称上下色块，检查合成前后像素位置一致、PBR 与 Debug Draw 同坐标对齐、深度遮挡正确、
Canvas 方向不变，并覆盖两个后端及 FXAA 开关。上游修复后，Gneiss 更新固定依赖，重新检查投影契约、
背面剔除和编辑器场景／网格／Gizmo 对齐。本次未修改 Granit，也未验证修复后结果。

临时采集代码已经移除，重新编译 Runtime 后 `gneiss.runtime.smoke` 通过。现有冒烟测试没有上下方向
像素断言，之前测试通过不代表画面方向正确；此项仍是 M-236 的未完成验收。
