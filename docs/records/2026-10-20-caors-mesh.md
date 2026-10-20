<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS Mesh CPU 接入验收

## 范围与结果

Windows Clang Debug，CAORS 共享库；主开发配置同时启用输入、材质和 Mesh。
以下自动化通过：

- 新增 JSON Mesh 生命周期测试：移动前后顶点数组地址相同；跨批次同版本共享；
  同 URI 不同来源隔离；内容变化后新旧版本并存；无效来源回执拒绝；
  销毁 RID、批次及缓存后旧帧仍保活，最后引用释放后弱引用失效。
- 真实异步资产服务覆盖 cooperative/thread_pool：修改一个 Mesh 后，其余未变网格
  继续共享相同 CPU 对象；保留原上传失败、取消、依赖失败及旧版本保护测试。
- Application、材质文档、Editor 资产编辑、隐藏窗口冒烟、资产服务共 5 项通过。
- 增补重载断言后，资产服务及 core-boundary、core-boundary-negative、
  internal-header-visibility 共 4 项通过。
- 仅 Mesh 开启、输入和材质关闭的静态 Engine 构建、安装及 4 项消费测试通过；
  Mesh 关闭、材质开启的共享 Engine 构建、安装及 4 项消费测试通过。

日志位于本地 build/caors-mesh-tests.log、build/caors-mesh-final-tests.log、
build/caors-mesh-sc-mat-s-consumer.log 和 build/caors-mesh-sc-mat-h-consumer.log，
不提交生成日志。

## 限制

默认关闭 Mesh 选项。本轮重点验证 JSON Mesh 的共享和生命周期，
未单独增加二进制 Mesh 共享专测；二进制仍走原解析器。
未验证 Linux/macOS/Emscripten、Release、大型 Sponza 内存收益、可见窗口人工操作。
本次是 CPU 资源持有接入，不代表 CAORS 接管 GPU、纹理载荷、同步加载或场景采用事务，
不宣称读取、解析或主线程帧间隔已改善。
