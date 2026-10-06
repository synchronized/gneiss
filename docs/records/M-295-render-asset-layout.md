<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：Render 输入快照与 Asset 解码边界

日期：2026-10-05。完成下列依赖与布局子项，其他语义配置和加载接口继续迁移。

## 迁移依据与结果

| 对象 | 原问题 | 实际调整 |
| --- | --- | --- |
| Render 快照 | 帧包和 Granit 后端包含 World 的提取头 | 值类型归 Render，World 仅保留提取函数；Render 不再反向包含 World |
| PNG 解码 | 位于 Render，但 Cook 与加载均使用且无 GPU 依赖 | 迁至 Asset，命名空间、测试和构建目标同步迁移 |
| Granit Render Service | 后端在旧目录且使用 Application 命名空间 | 移至 `src/render/backend/granit/`，使用 `render_internal`，后端头改为 `.hpp` |
| Shader 与模型矩阵适配 | 同上 | 随对应 Render 后端迁移，算法与 Shader 资源不变 |

没有修改提取顺序、帧包数据、资源租约、候选发布、GPU 回执或预算语义。宿主现在显式包含 World
提取接口，不再通过后端头间接获取它。PNG 输入限额和错误语义未变，Cook 与运行时复用同一实现。
公共 C 接口、SDK 安装路径和 Granit 依赖版本均未变化。

## 依赖检查

内部边界检查新增 Render 对 World/Scene/Application 头的禁止规则及 PNG 对 Render 的禁止规则，
反例覆盖每条方向；这属于文本包含检查，不声称可替代完整 C++ 依赖图或链接验证。

## 验证

共享库全量构建与 CTest 173/173 通过（193.41 秒），包含编辑器/Runtime 工作流、Cook、
资产像素、帧包、旧 ABI、安装消费者以及新增边界正反例。静态库全量构建通过，相关回归
17/17 通过（30.33 秒），包括 PNG、Shader、投影、快照、帧包、资产加载及像素、安装消费者和边界检查。

静态检查保留 PNG 既有常量乘法提升、空输入诊断字符串分配位于 try 外，以及矩阵初始化列表
尾逗号建议；未在目录迁移中混入算法或错误行为改动。后续异常保护修复见下节。

未运行远端 Actions、Vulkan validation 或外部大场景测量；本轮没有修改上游 Granit。

## 未完成部分

`render_asset_loader` 同时连接 CPU 准备、缓存租约与 Render 发布，尚未拆为独立准备契约；不能整文件
改名后宣称职责已拆分。窗口原生信息仍由 Platform 定义，其旧 `application_internal` 命名空间尚未
迁移。Application 仍装配具体 Render Service；可替换后端门面和公开描述语义适配需要后续单独验证。
其余内部 `.h` 继续逐模块处理，不将本轮后端 `.hpp` 迁移描述为全仓完成。

## PNG 分配失败保护补充

目录迁移后单独修复 PNG 错误路径：诊断字符串分配进入 try，输出清理改为不分配内存的尺寸清零
和有效像素清空。MSVC Debug 的空 vector 可能分配调试代理，旧 `out_image = {}` 在进入 try 前
或 catch 清理中均可能终止进程。输出允许保留既有容量以避免清理阶段再次分配，不改变像素限额。

专项测试在选定解码调用期间持续拒绝标准分配，覆盖像素分配和空输入诊断分配；标准库若用短字符串
存储整个诊断，则验证无需分配的 INVALID_ARGUMENT 路径。初版测试确实触发旧清理路径的固定
终止码 99；改为无分配清理后通过。测试仅替换本测试进程的分配入口，不改变引擎分配器。

共享库与静态库相关目标重新构建通过，PNG/资产加载/Cook 各 3/3 通过（0.57 秒 / 0.46 秒）。
解码器的 exception-escape 告警消失；保留既有常量乘法提升告警。测试的
CRT 参数命名差异、main 分配异常提示和既有 PNG 数组排版建议不通过关闭检查掩盖。
