<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：准备产物版本键

2026-10-15，继续 [共享资产与场景实例实施设计](../plans/asset-scene-ownership.md)。
版本编码决策见 [ADR-064](../decisions/ADR-064-shared-assets-and-scene-instances.md)。

## 已实现

prepared_asset 在整批来源复验成功后生成 asset_version_key，resource_cache 直接使用同一类型，
不要求准备模块依赖缓存实现。现有来源、描述及纹理负载摘要组成内容版本；纹理带准备配置，
材质带各槽位的纹理版本。没有附加文件读取或负载重哈希。

键保留输入来源身份列表，防止同内容不同来源混用，以及来源标记销毁后地址复用造成误判。
缓存校验输入身份合法性，哈希与相等比较使用相同字段。原 URI 热重载路径保持原行为。

## 验证

Windows Clang Debug 相关目标构建成功，定向 CTest 7/7 通过：resource_service、
render_asset_preparation、texture-load-service、scene-load-application、render_asset_loader、
core-boundary、core-boundary-negative。

新增测试将真实准备结果送入固定版本缓存，验证重复准备只加载一次、纹理负载变化导致材质
版本变化且新旧共存、准备配置改变版本、独立来源隔离、仅负载覆盖挂载时传递来源隔离，
以及非法输入来源清空输出。原取消、复验失败和线程调度测试继续通过。

格式检查通过。修改实现执行 clang-tidy，新增代码告警已修正；asset_preparation.cpp 其他
区域仍有复杂度等告警，不宣称全文件静态检查通过。未运行完整 Sponza 或跨平台发布矩阵。

## 未完成

实际渲染加载器仍使用旧发布路径；测试闭环不代表跨场景共享已完成。下一步需将固定版本
引用接入独立候选 RID、依赖租约和场景切换，再调整缓存所有权。共享任务订阅与取消隔离
仍未实现。没有远端操作，也未修改公共 SDK。
