<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 资产加载协议收尾验收

日期：2026-11-08。对应 [PLAN-107](../plans/PLAN-107-asset-model-loading-protocol.md) 第二至第六阶段。
当前接口见 [资产参考](../reference/assets.md)，决策见 [ADR-112](../decisions/ADR-112-asset-loading-protocol.md)。

## 完成范围

- 管理器统一推进 content/reference 依赖；弱记录图检查直接、间接、并发根与已有内容的强拥有环，诊断保留 URI 链和属性路径。
- Scene 等待 Prefab 内容，材质绑定纹理身份；纹理内容失败不回滚材质，非空失败引用保留 URI。
- 通用对象通过序列化上下文收集引用、统一调度后重新绑定；解析器删除资产类型 switch，自定义 Loader 使用同一声明/装配协议。
- 公开入口统一为 load/reload/create/find；五类资产支持只读 CPU 内容，匿名 Scene/Prefab 创建不登记 URI 缓存。
- 请求 snapshot 固定本次成功内容，resource/take 返回共享身份；依赖结果未领取期间的再次重载也不改变本次内容。
- CPU 对象立即析构；仅 RID 释放令牌入队，后端仍负责 GPU 在途安全回收。
- 文件槽位满时保留请求并重试，避免读取空请求；过期索引重新预留身份时清除旧就绪状态和拥有边。
- 取消只放弃观察者；资产使用者继续保活加载。显式 store 替换后，旧操作以 invalid_state 结束，不再采用内容。
- 调用方、样例、文档及场景私有构造边界检查同步迁移。Scene Access 仅允许类型 Loader 与管理器匿名创建使用。

## 验证

Windows Clang Debug 默认构建成功，警告作为错误；git diff --check 通过。
集中定向 CPU 回归覆盖 47 项：资产管理、Scene/Prefab、序列化、输入、编辑器相关流程、销毁队列及模块边界。
首轮 45/47 通过；旧容量拒绝/取消/被替代操作断言和匿名创建白名单更新后，失败项全部通过。
边界反例仍通过，未关闭检查或跳过失败用例。最后的代码调整仅涉及测试与边界规则，未重复无关测试。

关键新增用例覆盖单文件槽位下依赖排队、并发依赖环、成功内容形成强环的拒绝、缺失纹理 URI 保留、
固定请求及依赖快照、过期弱索引复用，以及未消费后端销毁队列时 CPU 材质/纹理引用已经释放。

本地日志：build/plan107-acceptance-build.log、build/plan107-acceptance-tests.log、
build/plan107-final-check-tests.log、build/plan107-final-manager-build.log、build/plan107-final-manager-tests.log。
未运行 GPU、窗口、大场景、跨平台共享/静态矩阵或远端 CI；未推送、创建 PR 或发布。

## 保留边界

- 纹理/网格/材质仍须 attach_render 创建所属 Render 的 RID；本次没有无 Render 装配或流送。
- 通用反射引用绑定会重解析内存字节一次，不重复 I/O；这是避免保留失效容器元素地址的取舍。
- 自定义 Loader 必须声明全部强引用依赖并正确实现 reserve/adopt；图不检查用户私自创建的隐藏拥有关系。
- 旧 register_decoder 的自定义值资产仍可产生新不可变对象；稳定身份扩展应使用完整 Loader 协议。
- 以上实现完成不等于 DEV-052 已完成跨平台或发布验收。
