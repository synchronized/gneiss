<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-272：Intel Sponza 审计与同步基线

2026-09-25 实施中。本记录包含已经测得的数据，不代表 0.42 异步流程已完成。

## 夹具身份与兼容性

来源为 [Intel 官方样例页](https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html)
的 Sponza Base Scene；获取入口 `https://cdrdv2.intel.com/v1/dl/getContent/830833`。
原始 ZIP 为 3,987,608,266 字节，SHA-256：
`b8bb853884ab1566b3beb35666bd09882a4e0dc16661e4684e103792cf0229b9`。
缓存位于仓库外，原包与派生产物均不加入源码 Release。

入口为 `main_sponza/NewSponza_Main_glTF_003.gltf`；155 个源节点、115 个 Mesh、405 个 Primitive、
3,747,018 个三角形、28 个材质、72 张 PNG。25 张基础颜色纹理均为 4096×4096，RGBA8 解码后
合计 1,677,721,600 字节。未使用的法线等图片也随离线导入输出保存，但不能把它们算作当前运行时
实际依赖。没有必需扩展、动画、蒙皮或矩阵节点。

原样保留 `credits_license.txt` 的署名和完整文本。该文件同时包含用途说明与 CC BY 4.0 正文；
本记录不把它改写为无条件商用授权。测试输入和本地派生目录携带原始文本，源码不重新分发资产。

实际发现并修复两个接入问题：

- 63,731 个顶点使用范围外 UV，最小约 -42469.96875、最大约 22.498。原 Gneiss 将 UV 限为
  `[0,1]`，与重复寻址采样器不一致。修复同步 JSON/二进制加载、资源创建和后台准备校验，保留原值，
  继续拒绝 NaN/无穷值。
- 外部 PNG URI 未填写 MIME。导入器过去误拒绝该情况；现在对已读数据检查 PNG 签名，仍拒绝伪 PNG。

当前渲染差异由准备工具写入 `audit.json`，转换工具继续携带：仅基础颜色贴图参与着色；法线、
金属度/粗糙度贴图、第二组 UV、顶点颜色与灯光扩展没有完整映射；`dirt_decal` 的 BLEND 效果
不等同原始透明材质。固定相机恢复自源节点 149（PhysCamera001）。这些限制不通过删除对象隐藏。

## 转换与重复运行

工具使用 Python 3.13；日常配置缩图固定使用 Pillow 11.3.0，最大边长 1024，保留层级、网格和全部图片引用。
完整配置不缩图。获取原始包后，可用下列步骤复现，输出目录必须是新的目录：

```powershell
python tools/performance/prepare_sponza.py <source.zip> --profile daily --output <daily-source>
python tools/performance/prepare_sponza.py <source.zip> --profile full --output <full-source>
python tools/performance/import_sponza.py <daily-source> <gneiss_assetc.exe> --output <daily-assets>
python tools/performance/import_sponza.py <full-source> <gneiss_assetc.exe> --output <full-assets>
<gneiss_scene_load_baseline.exe> <daily-assets> <existing-output-directory>/daily-1
<gneiss_scene_load_baseline.exe> <full-assets> <existing-output-directory>/full-1
```

工具校验原始 ZIP 哈希、成员哈希和渲染实例数，保存转换器二进制哈希、转换耗时及相机信息。
两配置均生成 553 个节点（包含多 Primitive 合成子节点）、405 个渲染对象及 405 个 Mesh 文件。
本次 Debug 离线转换分别为 8.07 秒和 27.22 秒；导出目录分别约 241 MB、2.14 GB，包含未被材质
引用的图片，不能直接代表驻留量。

## 已测同步基线

Windows、Intel i5-8600K、32 GiB RAM、Intel UHD 630（驱动 31.0.101.2140），Clang Debug。
每次启动独立进程；未清理操作系统文件缓存，因此属于系统缓存可能温热的重复采样，不宣称冷盘测试。
测量期间没有并行编译。加载时间仅包括公共同步 Scene 加载；不混入离线转换、窗口创建或 GPU 回读。

| 配置 | 同步加载三次（毫秒） | 进程峰值驻留范围（GiB） | 节点/渲染对象 |
| --- | --- | --- | --- |
| 日常 1024 | 4730.42 / 4519.10 / 4536.68 | 0.857～0.867 | 553 / 405 |
| 完整 4096 | 35722.45 / 35893.48 / 36153.94 | 3.696～3.697 | 553 / 405 |

GPU 回读图确认场景和固定相机实际可见；存在已声明的透明贴花与着色差异。回读计时约 0.50～0.79 秒，
不作为帧率。`render_60_ticks_ms` 是 60 次主循环加队列排空时间，三次各只累计呈现 7 帧，**不能解释为
60 个渲染帧**。进程工作集包含驱动映射的影响，不是独立显存计数；本次没有独立显存峰值数据。
Release 和异步两模式采样仍待执行。

## 后续门槛依据

完整负载在本机可运行，但新旧场景并存会显著提高峰值。后续预算与验收门槛固定在
[版本计划](../plans/VER-042-0.42.0-async-scene-loading.md)，不得在得到优化结果后倒设通过线。
本记录后续补充 Release 同步样本；异步交互、切换和故障结果使用独立验收记录。


## 异步基础原型预验收（非最终性能结论）

2026-09-25，Debug 全量构建与 156 项 CTest 通过，包括新增纯描述准备、候选域隔离、
Application 两种执行模式、源版本变更拒绝和子批次容量不足回滚测试。

日常配置一次连续主循环预验收：加载 50,048.75 ms，后台资产准备 27,257.49 ms，最终源验证
10,118.86 ms，候选 CPU 资源 215,395,096 字节，主线程单次推进最大 2.90 ms，激活 1.05 ms。
事件处理间隔 P95 19.42 ms、最大 62.55 ms，进程峰值驻留 920,399,872 字节，553 节点成功激活并回读。
该样本尚未覆盖 Runtime/Editor 宿主、完整配置或三次重复，不作为 M-277 通过结论；
Debug 摘要验证的额外耗时需要与 Release 分开报告。

早期测量工具逐次调用 `run(1)`，每次返回都等待渲染队列清空，测得事件间隔最大 311.94 ms。
该执行方式不是正常主循环，已改为单次连续运行、在 update 回调内推进并记录间隔；原始失败结果
保留在本地 `build/0.42-baseline/daily-debug-thread-probe.*`，连续运行样本为
`build/0.42-baseline/daily-debug-thread-loop.*`。未通过修改门槛覆盖该差异。
