<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Meta 读取契约与消费者回归

## 范围与结论

本批实施 [PLAN-118](../plans/PLAN-118-error-contracts.md) P2 首条读取链，
没有完成整个 P2，也未扩展到 Render 或 Prefab 重放错误分类。
当前接口规则以 [反射参考](../reference/reflection.md) 为准。

`read_value()` 仍返回 `result<value>`：用户 getter 可显式失败，部分类型不支持值读取，
数值与文本也可能非法。属性面板 `reflected_property_editor.cpp` 及 Runtime 面板已有
读取失败处理，继续保留；不为简化外观删除真实失败分支。

## 改动

- `core/meta/instance.cpp`：空实例读取及空成员存储地址改为契约断言；标量操作表返回
  错误 kind 同样断言；合法类别但内容非法返回 invalid_argument。
- `core/meta/registry.cpp`：getter 成功值必须匹配登记类别，显式失败原样传递。
  原始类型擦除入口的空对象参数、未知属性和能力缺失继续返回诊断。
- `core/serialization/traversal.cpp`：枚举名称编码保留 getter 的原错误码，
  外层遍历继续添加字段路径；不再统一覆盖成 invalid_argument。
- 测试移除空实例读取返回 invalid_state 的旧预期；新增代表性断言子进程与错误传播回归。
  沿用已有空引用、回调异常和生命周期验证，不为每个断言复制一套测试。

## 验证

Windows Clang Debug 六个相关目标构建通过，七项定向测试通过：property_access、
meta-read-contract、meta-instance、reflection-json、serialization、property-inspector、
editor.component-edit。

Windows MSVC Release 三个 Meta 目标构建通过，property_access、meta-read-contract、
meta-instance、reflection-json 四项定向测试通过。

断言子进程保留 Engine DLL 的默认诊断，在 SIGABRT 处退出为 86；检查表达式与退出码，
不把任意崩溃当成成功。测试不替换生产断言处理，也不新增生产测试钩子。

本批未运行全仓矩阵、静态库、跨平台或 GPU 验证；这些结果不能由定向回归推断。
