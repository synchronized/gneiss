<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景来源捕获调用方迁移

宿主的 Prefab 重载筛选改为读取场景激活回执中的依赖 URI，不再调用运行场景的
capture_description。记录仅属于宿主协调，不持有完整作者描述或资源强引用。
对应实例全部删除时，成功加载的来源应用为空操作；加载失败仍按原错误返回。

编辑器测试的保存内容来自 capture_content；需要验证实际 ECS 组件身份时直接使用
capture_components，避免用文档内容替代运行状态断言。

Windows Clang Debug 默认构建通过，9 项定向 CPU 回归通过，未运行 GPU 场景或远端 CI。
本记录仅覆盖调用方解耦；旧运行捕获接口、Prefab 来源长期状态与持久父地址仍待迁移。
