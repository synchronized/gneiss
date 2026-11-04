<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：资产运行期来源身份

2026-10-15，首批身份基础实现。依据 [ADR-064](../decisions/ADR-064-shared-assets-and-scene-instances.md)。
此项不代表共享缓存或固定版本发布已经完成。

## 实现

新增内部 `asset_identity`，由来源域标记、合法 URI、非零类型组成。VFS 每次成功新挂载创建
独立标记；复制 VFS 保留原标记。`identify` 按读取相同的最长挂载前缀规则解析，返回身份，
不读取文件或检查文件存在性；失败清空输出。类型不同或 URI 大小写不同不会被合并。

标记使用共享持有的空对象，不是持久 GUID，也不含文件后端或设备。使用者持有身份时不会发生
标记地址释放再复用的误判；身份不保证对应来源永远有效，也不延长后端寿命。
独立挂载同一个后端保守地视为不同来源；副本共享与跨项目路径混用明确区分。
挂载后不得将后端重新指向别的来源，重配需要建立新挂载；内容变化则由未来版本键处理。

没有修改 resource_cache 的 URI 键、既有 RID 更新、显式热重载或公共 SDK。
该接口只落实来源身份，不校验源内容版本，不能据此直接跳过来源复验。

## 验证

Windows Clang Debug 构建 Engine 及受影响定向测试成功；CTest 6/6 通过：resource_service、
scene-load-application、render_asset_loader、source-revision、core-boundary、core-boundary-negative。
新增用例覆盖未挂载、未初始化后端的无 I/O 查询、VFS 副本、独立挂载、覆盖挂载、类型及大小写
区分、非法 URI/零类型输出清理，以及 VFS 销毁后的身份标记寿命。
所改 C++ 文件格式检查通过，virtual_file_system.cpp 的 clang-tidy 无诊断；
文档相对链接与 `git diff --check` 通过。

## 后续接入

缓存尚未消费此身份，跨场景加载去重与版本发布仍待实现。尤其需要在来源复验包装创建新的
VFS 挂载时传递原来源身份，不能把内部包装身份误当成用户来源，也不能借共享身份绕过复验。
后续将把源摘要、依赖版本和准备配置与逻辑身份分开表达，再迁移共享缓存。
未运行 Linux、静态矩阵、安装消费者或完整 Sponza 性能对照；未触发远端操作。
