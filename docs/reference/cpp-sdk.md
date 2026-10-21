<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# C++ 调用边界与共享值类型

当前不提供公共 C++ 对象 SDK。外部 C++ 程序使用 C ABI；引擎、Runtime、Editor 内部直接
使用 src 中的原生 C++ 对象，内部头不安装、不承诺兼容。

`gneiss::sdk` 命名空间及其 Application、World、Scene、Registry、渲染资源所有者、
Prefab 刷新令牌和 Game Module 包装已删除。application.hpp、game_module.hpp 与总入口
`gneiss/gneiss.hpp` 不再存在，没有兼容转发或改名后的第二套 SDK。

保留的 `.hpp` 提供共享值类型、结果、ID、枚举、数据描述、显式 C 值转换及无状态输入/校验函数。
它们不拥有引擎对象，也不替调用方延长资源寿命。C API 创建的对象须在正确线程使用对应
销毁函数释放；示例中的 scope_exit 和测试中的句柄守卫只用于局部清理，不是公共接口。

外部调用示例：

```cpp
#include <gneiss/engine/application.h>

gneiss_application app = GNEISS_NULL_APPLICATION;
gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
if (gneiss_application_create(&desc, &app) == GNEISS_SUCCESS) {
  const auto run_result = gneiss_application_run(app, 3);
  const auto close_result = gneiss_application_destroy(app);
  // 调用方处理运行与关闭结果。
  (void)run_result;
  (void)close_result;
}
```

完整生命周期示例见 [Application/World 示例](../../examples/application_world/main.cpp)，
反射、保存及重新加载见[属性检查示例](../../examples/property_inspector/main.cpp)。
