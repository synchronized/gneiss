// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--build") {
    if (argc > 5 && std::string_view(argv[5]) == "build-success") {
      std::filesystem::create_directories("modules");
#if defined(_WIN32)
      constexpr std::string_view filename = "test_game.dll";
#elif defined(__APPLE__)
      constexpr std::string_view filename = "libtest_game.dylib";
#else
      constexpr std::string_view filename = "libtest_game.so";
#endif
      std::ofstream output(std::filesystem::path("modules") / filename, std::ios::binary);
      output << "fixture";
      return output ? 0 : 74;
    }
    std::fputs("fixture build failed\n", stderr);
    std::fflush(stderr);
    return 23;
  }
  if (argc > 2) {
    std::fputs("fixture runtime waiting\n", stdout);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
  }
  if (argc != 2) {
    return 64;
  }
  const std::string_view mode = argv[1];
  if (mode == "exit") {
    std::fputs("fixture stdout\n", stdout);
    std::fflush(stdout);
    std::fputs("fixture stderr\n", stderr);
    std::fflush(stderr);
    return 23;
  }
  if (mode == "wait") {
    std::fputs("fixture waiting\n", stdout);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
  }
  return 65;
}
