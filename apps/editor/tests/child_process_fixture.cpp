// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <chrono>
#include <cstdio>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
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
