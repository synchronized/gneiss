<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景对象身份迁移实施记录

日期：2026-10-26。对应 [PLAN-053](../plans/PLAN-053-scene-object-identities.md) 和 [ADR-086](../decisions/ADR-086-scene-object-identities.md)。

## 已完成

- 删除 World 全局 RID 注册、借用登记及转发服务；场景域通过 world_owner 独占对象，反射和属性编辑直接访问原生 World。
- 场景实例使用 scene_instance_id，节点内部统一使用 scene_node_id，Prefab 刷新操作使用 prefab_refresh_token。
- 实例复用现有代次槽位表算法，通过模板参数限定标识类型；保留所属域、代次和失效校验。
- 同步迁移 Runtime、Editor、Host、示例和测试调用方，加入标识不可隐式混用的编译期断言。
- 明确 World 借用随场景切换或关闭失效；场景文档同时检查当前 World 与场景实例，避免旧文档误认复用地址。

## 验证

- `cmake --build --preset windows-clang-debug -j4 -- -k0`：通过，包含已配置示例和测试目标。
- 修改的 C++ 文件 `clang-format --dry-run --Werror`：通过。
- `cmake/check_core_boundary.cmake`、`cmake/check_public_api.cmake`：通过。
- `git diff --check`：通过。

按用户要求未运行测试、GPU/大场景或 CI。测试代码已迁移并编译，但跨域拒绝、重复销毁、线程边界和场景切换行为仍需统一运行验收。

## 保留边界

渲染资源继续使用 RID；场景实例 ID 不拥有对象，实例管理器负责生命周期。World 指针是借用，调用方必须在所属 World 销毁前停止使用。Prefab 刷新 token 仅在所属场景实例内有效，不承担全局资源身份。未改动场景文件格式、GPU 资源所有权或引入新的对象注册系统。
