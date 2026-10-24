// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_cli.hpp"
#include "engine/core/result.hpp"

#include "package_archive.hpp"
#include "project_workspace.hpp"

#include "child_process.hpp"

#include <gneiss/app/project_description.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

int fail(std::string_view stage, gneiss::engine::result operation) {
  std::cerr << "Gneiss 工程操作失败：阶段=" << stage << "，结果=" << operation.code()
            << "，消息=" << operation.message() << '\n';
  return 1;
}

void usage() {
  std::cout << "用法：\n"
               "  gneiss_project create <工程目录> <工程名> [模板目录]\n"
               "  gneiss_project export <工程目录> <Runtime 路径> <输出目录> "
               "[debug|development|shipping] [--zip]\n"
               "  gneiss_project package <工程目录> <Runtime 路径> <输出目录> "
               "[debug|development|shipping] [--zip]\n"
               "  gneiss_project verify <发布包目录>\n";
}

bool parse_profile(std::string_view text, gneiss::host::project_build_profile& output) {
  if (text == "debug") {
    output = gneiss::host::project_build_profile::debug;
  } else if (text == "development") {
    output = gneiss::host::project_build_profile::development;
  } else if (text == "shipping") {
    output = gneiss::host::project_build_profile::shipping;
  } else {
    return false;
  }
  return true;
}

} // namespace

int gneiss::host::run_project_cli(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string_view command(argv[1]);
  if (command == "create" && (argc == 4 || argc == 5)) {
    gneiss::host::editor_project project;
    const auto operation =
        argc == 5 ? gneiss::host::create_editor_project(argv[2], argv[4], argv[3], project)
                  : gneiss::host::create_editor_project(argv[2], argv[3], project);
    if (!operation) {
      return fail("创建工程", operation);
    }
    std::cout << project.project_root.string() << '\n';
    return 0;
  }
  if (command == "verify" && argc == 3) {
    const auto operation = gneiss::host::verify_package_manifest(argv[2]);
    if (!operation) {
      return fail("校验发布包", operation);
    }
    std::cout << std::filesystem::path(argv[2]).string() << '\n';
    return 0;
  }
  if ((command == "export" || command == "package") && argc >= 5 && argc <= 7) {
    gneiss::host::project_description project;
    auto operation = gneiss::host::load_project_description(argv[2], project);
    if (!operation) {
      return fail("加载工程", operation);
    }
    gneiss::host::project_build_profile profile = gneiss::host::project_build_profile::development;
    bool create_zip = false;
    for (int index = 5; index < argc; ++index) {
      const std::string_view argument(argv[index]);
      if (argument == "--zip") {
        create_zip = true;
      } else if (!parse_profile(argument, profile)) {
        usage();
        return 2;
      }
    }

    gneiss::host::project_export_report report;
    gneiss::host::project_export_options options{.runtime_executable = argv[3],
                                                 .output_root = argv[4],
                                                 .profile = profile,
                                                 .create_zip = create_zip,
                                                 .asset_progress = {}};
    options.asset_progress = [](std::uint64_t current, std::uint64_t total, std::string_view path,
                                bool cache_hit) {
      std::cout << "资产 [" << current << '/' << total << "] " << path << "："
                << (cache_hit ? "缓存命中" : "已构建") << '\n'
                << std::flush;
    };
    operation = gneiss::host::export_editor_project(project, options, report);
    if (!operation) {
      return fail("生成发布包", operation);
    }
    std::cout << "资产构建：源=" << report.asset_source_count
              << " 新建=" << report.asset_built_count
              << " 缓存命中=" << report.asset_cache_hit_count
              << " 裁剪=" << report.asset_pruned_count << '\n'
              << std::filesystem::path(argv[4]).string() << '\n';
    return 0;
  }
  usage();
  return 2;
}
