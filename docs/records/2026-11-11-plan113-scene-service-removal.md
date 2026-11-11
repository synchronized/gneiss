<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：删除迁移期场景服务

日期：2026-11-11。

## 已完成

删除 runtime/loading 的四个文件及构建、内部接口清单引用。Application 不再装配 scene_service，
不再暴露 scenes、scene_loading、visual_assets_loading、can_activate_scene 或收取旧服务诊断。
World 仍由 Application 唯一拥有；Runtime/Editor 各自维护场景加载和视觉批次。
关闭流程等待文件服务与资产管理器收尾，再释放 World 和后端资源。

批次提交删除依赖场景状态的 allow_load 参数；can_load_assets 只查询应用是否接受请求、
执行器是否已配置及共享批次是否空闲。批次取消后仍须领取或释放回执才腾出槽位。
在既有生命周期测试补充上述状态检查，没有创建新的测试框架或重复场景用例。

删除只检查已删除服务成员的静态断言和专用边界反例；通用资产私有实现边界仍保留。
同步 Application、资产、场景、架构及目录文档的当前行为，历史记录不改写。

## 验证

Windows Clang Debug 全部开发目标编译通过。以下 8 项定向 CPU 回归通过：

- runtime.scene-loader、runtime.asset-reloader。
- scene_structural_reload、prefab_structural_reload。
- editor.scene-loader。
- native-application、scene-load-application、texture-load-application。

check_core_boundary.cmake、test_core_boundary.cmake 及 git diff --check 通过。
源码、测试、CMake 中无旧场景服务及其请求协议引用。
未运行 GPU、大场景、其他平台或远端 CI。

## 剩余

PLAN-113 尚需汇总全计划验收与版本状态；PLAN-114 的测试标签审核仍未完成。
