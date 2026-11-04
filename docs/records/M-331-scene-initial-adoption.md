<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：新场景初始版本采用

2026-10-15，在 [宿主重载采用](M-331-host-adoption.md) 基础上补齐新场景初始选择。

## 实现与边界

场景加载器从每批固定版本回执收集选择，沿用回执中的准备配置。只对首次插入的资产绑定
记录选择，跨材质批次共享依赖不重复采用；原有绑定一致性检查继续拒绝不同 RID。
版本数量与资产数量不匹配时请求失败。

来源复验、实例构建完成后，在私有候选缓存中一次采用全部选择，成功后才进入 ready。
无渲染依赖的场景跳过空批次。采用失败沿现有失败清理流程丢弃候选，当前活动 World 不变。
ready 后取消也销毁整个候选域及其选择。真正激活仍只转移域和对应资产服务的所有权，
不会在旧域已经退出后执行选择映射分配。

该实现依赖当前候选缓存独立于活动缓存的隔离条件。将来迁移到 Application 共享缓存时，
不能提前更新全局选择，须将候选选择准备和最终采用纳入实际激活事务。

## 验证

Windows Clang Debug 引擎与 scene-load-application 构建通过；定向 CTest 4/4 通过：
scene-load-application、texture-load-application、core-boundary、core-boundary-negative。
新增 ready 后取消与空场景激活反例，均覆盖线程池和协作模式；既有测试继续覆盖
共享纹理跨批次去重、来源改变失败、依赖损坏、取消清理及后续重试。

scene_load_service.cpp 执行 clang-tidy，仍有既有格式、条件表达式与 advance_impl 复杂度告警。
未执行 GPU、完整 Sponza、跨平台及安装矩阵，未执行远端操作。

Editor 编辑命令、Prefab、撤销重做等同步消费者尚待迁移；本轮不代表 0.51 完成。
