// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <caors/io/file_system.h>
#include <caors/tasks/external_executor.h>

namespace gneiss::engine::platform::io {
class uv_file_io;
class uv_async_file;
}
namespace gneiss::engine::core::tasks {
class task_executor;
}

// 仅供内部宿主集成；不安装、不导出，也不进入 Gneiss 公共 SDK。
namespace gneiss::engine::platform::caors {
class file_adapter : public ::caors::io::async_file {
public:
  virtual void pump() = 0;
  virtual void abandon() = 0;
};
class file_system_adapter : public ::caors::io::file_system {
public:
  /** 宿主线程串行、非重入推进；完成凭证在锁外交付。 */
  virtual void pump() = 0;
  /** 先停止工作 scope，再放弃回执、排空完成队列，最后停止借用的 loop。 */
  virtual void abandon() = 0;
};
class worker_adapter : public ::caors::tasks::external_executor_context {
public:
  /** 宿主定期回收本 scope 的任务完成记录。 */
  virtual void collect() = 0;
};
/** 文件自行保活；宿主保持 loop 运行至所有在途操作清理。 */
std::shared_ptr<file_adapter> make_file(std::shared_ptr<engine::platform::io::uv_async_file> file);
/** 借用 io，不创建线程池。必须在 io/loop 销毁前 abandon 并排空。 */
std::shared_ptr<file_system_adapter> make_file_system(engine::platform::io::uv_file_io& io);
/** 借用调度器；close 只能由宿主调用，等待独立 scope 排空。 */
std::shared_ptr<worker_adapter> make_worker(engine::core::tasks::task_executor& executor);
} // namespace gneiss::engine::platform::caors
