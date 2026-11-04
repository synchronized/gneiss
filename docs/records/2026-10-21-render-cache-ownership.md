<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 渲染缓存与资产管理器所有权分离

2026-10-21，0.51 特性分支；接续[上传通道分离](2026-10-21-render-channel-boundary.md)。

## 当前对象关系

```text
Application
  asset_manager
    输入 CAORS 状态
    CPU 文档缓存
    Prefab 缓存与 Loader
  render_asset_cache
  render_asset_loader → VFS、render_asset_cache、render_resource_service
  scene_session → asset_manager、render_asset_loader
    scene_load_service → 相同资产管理器与渲染加载器
    活动/候选 application_scene_state → 相同加载器
```

asset_manager 的构造不再接收 render_resource_service，头文件和实现不引用 Render。
删除渲染 acquire/reload、render_loader 访问器以及版本选择转发接口。
原通用缓存中的渲染条目由 Application 独立持有的 render_asset_cache 接管；资产管理器
剩余旧缓存明确命名为 prefab_cache。两者按资源职责划分，不重复存储同一种租约。

不新增总管、locator 或兼容分支。渲染加载器继续借用缓存与服务，活动及候选场景显式借用
同一个渲染加载器。场景加载只向渲染缓存准备版本选择，不更改 Prefab 默认版本。

## 生命周期

正常关闭先取消输入与停止资源工作，再排空场景请求并释放活动/退休场景；随后销毁渲染
加载器、渲染缓存和资产管理器。render_resource_service 的存活期覆盖上述对象。
成员声明顺序也保证初始化中途失败时场景先于其借用对象析构。

运行中的回收先释放无使用者渲染租约，再清理 Prefab/CPU 文档，避免缓存保留已无人引用的
文档索引。用户仍持有的租约与在途渲染帧沿用现有寿命契约，不因场景销毁提前失效。

## 验证

- Windows Clang Debug 的 Engine、相关测试与 Editor model 构建成功。
- 6 项 CTest 通过：scene-load-application、prefab-preparation、texture-load-application、
  scene-load-builder、core-boundary、core-boundary-negative。
- 新增 Prefab 获取不改变 Render 版本戳的断言；保留渲染租约回收、跨服务拒绝、候选隔离、
  失败不采用与重载引用切换测试。
- 原生无线程静态 CAORS Application 构建与测试通过。
- scene_session 与 application_scene_state 的 clang-tidy 无诊断；格式与 diff 检查通过。

未执行 GUI、Linux/Web、完整安装与发布矩阵，公共 API/ABI 未改。

## 剩余边界

资产管理器仍使用 function/scene 的 Prefab Loader，并维护尚未统一到 CAORS 的 CPU 缓存。
因此暂不将管理器移动到禁止功能层依赖的 resource 目录。后续应收敛 Prefab Loader 归属、
CPU 缓存身份及请求发布，不把这次渲染所有权分离描述为整个资源系统完成。
