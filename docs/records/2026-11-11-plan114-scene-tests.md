<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-114：旧场景测试消费者收敛

日期：2026-11-11。

## 修改

scene_load_application_test 改用资产请求、内容快照、直接实例化和显式视觉批次。
删除候选阶段、预算字段及重复取消断言；取消/关闭由既有 native_application 覆盖。
保留 CPU 加载不修改 World、固定内容不重读磁盘、空场景、追加与显式卸载、材质失败重试、
缺失纹理降级、资源共享和显式重载身份稳定性。缓存复用用损坏源文件验证。

来源重载的增删对象、换根、运行变换保留、失败重试及主相机选择合并到现有宿主测试。
删除无人调用的 observe_request 测试辅助。tests 中已无 scenes()、scene_open_request、
scene_load_progress 或 scene_load_completion 旧协议引用。

## 验证与限制

Windows Clang Debug 两个受影响测试目标编译通过；scene-load-application 与
runtime.scene-loader 定向 CPU 测试全部通过，覆盖线程池与协作模式。
未运行 GPU、大场景、其他平台或远端 CI。

生产旧服务删除属于 PLAN-113 后续工作；PLAN-114 的辅助代码审查与测试标签审核尚未完成。
