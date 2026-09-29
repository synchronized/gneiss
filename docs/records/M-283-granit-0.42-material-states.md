<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-283：Granit 0.42 材质状态接入

## 范围

2026-09-29，Granit 锁定 `29a4f18a67a8f506c585f0f515d93ddc406d7426`（v0.42.0）。
上游 PR #117 的实现进入 Gneiss 数据、导入、资产加载及 GPU 路径；没有修改 Granit 仓库。
当前格式见 [Render 资产格式](../reference/render-asset-formats.md)，协调状态见
[UPSTREAM-043](../plans/UPSTREAM-043-granit-pbr.md)。本记录不是 0.43 发布验收完成声明。

## 实现与修正

- Material v5 保存 OPAQUE/MASK/BLEND、双面、cutoff 和五槽独立 UV/采样设置。
- Mesh Binary v3 保存 UV1 与线性顶点色；C 描述保留前两版布局，第三方类型不进入 ABI。
- GPU 顶点统一为 72 字节，缺失属性填零 UV/白色，选择对应 UV1/Color 标准变体。
  初次接入误用 48 字节变体导致几何不可见，像素回归发现并修正。
- 每材质拥有五个采样器；负缩放绕序/手性、MASK 与透明阶段交由 Granit 公共管线。
- 导入器版本升至 3，缺切线按法线贴图 UV 集生成；有限但退化的作者切线重新用 MikkTSpace
  生成，并输出诊断。非有限值及非法手性仍拒绝。正交化使用 double 中间值避免近共线消减。
- Sponza 有一个 Primitive 需要重生成切线；一个自发光分量为 `1.00000012`，仅允许将
  `nextafter(1,2)` 以内的一 ULP 越界归一为 1，并记录诊断。运行资产仍严格校验 0..1。

## 本地证据

Windows Clang Debug / Intel UHD 630：

- 共享库完整 CTest：162/162，170.08 秒（材质状态接入后，Sponza 输入修正前）。
- 输入修正后专项：9/9，11.95 秒；包含 glTF、Writer、Mesh Binary、材质资源、Loader、GPU 与负缩放。
- 追加透明层次后的像素测试：1/1，11.52 秒。检查 MASK 阈值、顶点 Alpha、双面可见性、
  UV0/UV1、repeat/clamp/mirror、BLEND Alpha 0/0.5、两层透明提交顺序无关、不透明前景遮挡。
- 原有像素测试继续检查五纹理通道、Mip、异步重载/场景候选与正负缩放。

Sponza 日常输入沿用 0.42 审计身份，另写 `0.43-daily-assets-2`，没有覆盖旧基线。
导入 13.07 秒，405 个 Mesh，553 个节点；输出约 322 MB。一次线程池 Debug 实测：

| 指标 | 结果 |
| --- | ---: |
| 场景激活前总时间 | 85.70 s |
| 资产准备 / 来源校验 | 51.64 / 20.79 s |
| 上传阶段 | 933.17 ms |
| 激活 / 最大主线程推进 | 0.83 / 2.26 ms |
| 事件间隔 P95 / 最大 | 16.41 / 40.67 ms |
| CPU 候选资源量 | 412,533,944 B |
| 进程峰值 RSS | 1,454,600,192 B |
| 激活后资源数 | 505 |

回读图像显示完整庭院几何和材质；未将其作为黄金图或与可信参考做误差验收。
这是单次 Debug 样本，不代表性能门禁通过，RSS 也不是独立 GPU 显存。
本地证据位于 `build/0.43-pbr-sponza/daily-debug-2.{json,csv,ppm}`；不随源码分发原始资产。

## 未完成验收

静态库/安装消费者复验、Linux/Sanitizer/Web 发布矩阵、完整 Sponza 预算与重复采样、
连续切换/取消重试/窗口退出，以及双面照明与 MASK 阴影轮廓仍需完成。
完整输入仅基础 RGBA8 贴图已超过现有 2 GiB 候选预算，不能以静默丢图或临时抬高预算代替验收。
