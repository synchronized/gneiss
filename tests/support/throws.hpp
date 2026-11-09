// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
namespace gneiss::test_support {
// 仅匹配预期异常；其他异常继续传播，避免把测试缺陷当成成功。
template <class Exception, class Function> bool throws(Function&& function) {
  try {
    function();
  } catch (const Exception&) {
    return true;
  }
  return false;
}
}
