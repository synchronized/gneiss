<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Meta 操作表拆分验收记录

2026-11-04，分支 `feature/0.52-architecture-cleanup`。
本记录仅覆盖用户确认的 Meta 首批改动，不代表 0.52 全部完成。

## 交付

- 删除 `detail/type_access.hpp`，使用 `detail/type_ops.hpp`；类别 variant 指向每类型共享的静态操作表。
- 在 `meta/ops/` 分离 scalar、sequence、associative、optional、reference；optional 不再借用序列 resize。
- 自定义序列、字符串键映射通过 traits 适配，修改能力可选；不新增运行期注册中心或任意指针所有权适配。
- 保留现有引用保活、序列化格式和 codec 优先规则；不可默认构造元素允许读取，不支持的构造操作返回 unsupported。
- 更新 CMake 清单、所有调用方及[容器接口参考](../reference/reflection-containers.md)。

## 验证

- Windows Clang Debug 全量编译通过；Meta、反射 JSON、序列化、场景/Prefab 描述、编辑器相关及边界检查共 24 项通过。
- 最后调整测试中的 float 字面量后，重新构建并运行 meta-type-ops，通过。
- Windows MSVC Release 构建四个相关测试目标通过；meta-type-ops、meta-container、meta-instance、reflection-json 共 4 项通过。
- 新测试覆盖自定义容器 JSON 往返、排序、只读能力、越界、异常映射，以及非默认构造元素的序列、映射和 optional。
- src、tests、cmake 无 type_access/access_of 残留；git diff --check 通过。

未运行 Linux/GCC、Sanitizer、远端 CI、大场景或 GPU 工作流；完整版本矩阵留待版本候选验收。
traits 返回的元素指针仍为借用，结构修改后的失效由调用方遵守；本轮不增加悬空检测或事务回滚。
