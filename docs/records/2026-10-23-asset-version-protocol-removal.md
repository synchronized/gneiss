<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 资源版本与采用协议删除

日期：2026-10-23。

## 已实施

- 场景加载、普通资源重载、编辑资产和 Prefab 编辑统一使用稳定资源引用。
- 删除 Runtime 的 accept_edit_assets 及宿主适配，编辑命令直接提交作者修改。
- 删除 scene_session 的旧引用捕获、纹理关联材质扩展、整批 ECS 重绑和采用流程，
  以及已无生产调用者的 scene_instance_service::replace_render_assets 事务。
- 删除 fixed_version/publication_mode、版本发布/选择/采用接口、缓存选择表、采用预约及
  全局缓存 revision。缓存保留 URI 查找、加载、单资源替换与回收。
- 删除 asset_version_key、prepared_asset 的依赖版本链与版本摘要构建；来源摘要校验保留。
- Prefab 已解析描述更新普通缓存，旧实例持有其描述；不再使用通用版本索引。
- 删除仅验证旧协议的测试，保留来源身份、跨服务拒绝、资源生命周期及真实像素验证。
  测试按新契约检查同 RID 更新、纹理请求不重读材质、ECS 改绑不被覆盖，以及取消场景
  切换不回滚已生效的共享资源。

## 验收

Windows Clang Debug 构建通过；协议删除后的完整回归 201/201 通过（192.63 秒），
包含安装消费、接口边界、编辑器与 GPU 像素测试。静态检查无错误，仍有既有复杂度等告警；
`git diff --check` 通过。未运行其他平台及静态库构建矩阵。

## 未完成的重建范围

场景目前仍等待视觉准备和来源复验；临时上传结构仍使用 asset_candidate/stage/publish
组织，尚未完成任务内部收缩。CPU 准备及后续错误仍可终止剩余批次；逐项结果未贯通 IPC。
内容共享缓存仍保留来源证据，不能把它描述为整个资源系统重建完成。
后续验收以[资源简化计划](../plans/PLAN-051-progressive-render-assets.md)为准。
