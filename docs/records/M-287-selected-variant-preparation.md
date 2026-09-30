<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-287：后台单变体准备验收

## 本轮实现

2026-09-30 将 Application 的异步纹理及场景加载接到设备能力快照。
渲染线程初始化后发布 Gneiss 自有的四种格式能力和设备代次，后台按 Manifest 顺序选择同时支持
sampled / transfer destination 的首个变体。工作线程不访问 renderer。

容器读取 Header、Manifest 和选中 Payload；候选只持有 Manifest、选中索引及该变体的完整 Mip
负载。负载摘要、颜色空间、二维单层约束、全部变体边界及 RGBA8 回退要求继续校验。
候选预算在负载分配前检查。上传使用 Granit 0.44 的独立变体入口，并再次核对设备身份和选择结果。

异步缓存命中会核对纹理及材质纹理依赖的设备身份；逻辑层不指定 profile 时仍可读取已经发布的
RID。源版本账本保持分块哈希复验，纹理源不再进入批内整文件副本缓存。
`prepared_render_batch::input_bytes` 仍表示整文件快照持有的输入字节，不代表读取 I/O 总量。

## 验收

- CPU 夹具同时提供 BC7 / RGBA8：模拟设备分别选中 16 / 64 字节负载，RGBA8 位于非零偏移。
  BC7 候选可在小于整个容器大小的预算内准备；同预算拒绝更大的 RGBA8 候选。
- 覆盖不支持格式、选中负载摘要损坏、取消、结束校验时源变化，以及不同设备 profile 拒绝缓存复用。
  读取跟踪证明单变体准备本身不读取其他负载；源版本校验另计。
- GPU 像素夹具通过异步场景服务加载非零偏移的 RGBA8 变体，检查缩小后的红色低 Mip。
  同步兼容路径继续运行相同夹具。

Windows Clang Debug 共享库全量构建通过，相关回归 9/9 通过（15.68 s）。
静态库构建资产加载、纹理服务及像素测试目标通过，三项回归 3/3 通过（15.66 s）。
两轮均启用 `VK_LAYER_KHRONOS_validation`，检查 CTest 原始日志没有 Validation Error / VUID。

修改的四个核心实现文件运行了按变更行筛选的 clang-tidy；该检查不是全仓静态检查。
`git diff --check` 通过。日志保存在本地 `build/0.44-texture-residency/`：
`selected-build.log`、`selected-regression.log`、`selected-static-build.log`、
`selected-static-tests.log` 和 `selected-tidy.log`。未触发远端 Actions。

## 限制与后续

- 同步加载没有绑定设备快照，仍保留整包兼容路径；PNG / KTX2 不受这次选择优化影响。
- 完整性校验仍分块读取整个源文件，不声称只发生选中负载的磁盘 I/O。
  嵌套版本账本会重复哈希；本轮未优化该成本，也未测量大资产取消延迟。
- 所选负载上传后仍驻留 CPU；可重建来源、释放时机及预算生命周期属于 M-288 / M-289。
- 源版本复验不是跨进程原子快照。未验证完整 4K Sponza、峰值内存、Linux 或发布矩阵。
- BC7 选择为 CPU 注入能力测试；本轮新增真实 GPU 夹具验证的是 RGBA8，不混称 BC7 图像验收。
