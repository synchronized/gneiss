// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_workspace.h"

#include <gneiss/app/project_description.h>

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

int fail(std::string_view stage, gneiss::result operation) {
  std::cerr << "Gneiss 工程操作失败：阶段=" << stage << "，结果=" << operation.native()
            << "，消息=" << operation.message() << '\n';
  return 1;
}

void usage() {
  std::cout << "用法：\n"
               "  gneiss_project create <工程目录> <工程名> [模板目录]\n"
               "  gneiss_project export <工程目录> <Runtime 路径> <输出目录>\n";
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string_view command(argv[1]);
  if (command == "create" && (argc == 4 || argc == 5)) {
    gneiss::editor::editor_project project;
    const auto operation =
        argc == 5 ? gneiss::editor::create_editor_project(argv[2], argv[4], argv[3], project)
                  : gneiss::editor::create_editor_project(argv[2], argv[3], project);
    if (!operation) {
      return fail("创建工程", operation);
    }
    std::cout << project.project_root.string() << '\n';
    return 0;
  }
  if (command == "export" && argc == 5) {
    gneiss::app::project_description project;
    auto operation = gneiss::app::load_project_description(argv[2], project);
    if (!operation) {
      return fail("加载工程", operation);
    }
    operation = gneiss::editor::export_editor_project(project, argv[3], argv[4]);
    if (!operation) {
      return fail("导出目录包", operation);
    }
    std::cout << std::filesystem::path(argv[4]).string() << '\n';
    return 0;
  }
  usage();
  return 2;
}
