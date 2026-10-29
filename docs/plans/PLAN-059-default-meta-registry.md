<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-059：默认 Meta 注册表与统一启动

## 状态与目标

已完成。按 [ADR-092](../decisions/ADR-092-default-meta-registry.md) 统一模块默认注册表，保留隔离能力。
不实现动态注册、插件卸载或全局对象反射库；不修改资源所有权。

## 实施与验收

- [x] Core 增加共享默认表与无 registry 的注册、查询、属性访问、序列化重载。
- [x] Function 集中登记内置组件、资源 codec 和项目类型，随后统一冻结。
- [x] 应用、场景、资源管理器和编辑器共享默认表；删除资源专属静态表。
- [x] 独立示例与测试显式初始化；保留独立组件表用于隔离测试。
- [x] 验证冻结前查询、项目注册、默认属性和 codec、隔离表、重复启动、失败及异常语义。
- [x] 完成 Debug 编译与相关短测试，不运行大场景、GPU 验收或远端 CI。

## 使用方式

```cpp
// 在首次 application::create 前调用；不需要项目类型时可由 application 自动完成。
auto status = engine::initialize_meta([](const meta::registry_ref&) -> engine::error {
  return meta::type_builder<player_settings>{}
      .property<&player_settings::speed>(meta::property_id{1}, "speed")
      .commit();
});
// status 成功后：meta::describe_type<player_settings>() / meta::serialize(object)。
```

已有显式 registry 的接口继续可用；默认 getter 不代表内置类型已初始化。
单独使用 asset_manager 的程序应先调用 initialize_meta，或传入自行注册并冻结的独立表。

## 验收结果

2026-10-29：Windows Clang Debug 编译通过；39 项相关短测试通过，包括默认表生命周期、
并发查询、反射、资源加载、场景、编辑器及依赖边界。未运行大场景、GPU 验收、浏览器构建或远端 CI。
