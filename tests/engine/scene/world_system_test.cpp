// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <memory>
#include <stdexcept>
#include <vector>

int main() {
  using namespace gneiss;
  if (initialize_meta().failed())
    return 1;
  auto a = create_native(), b = create_native(), empty = create_native();
  if (!a || !b || !empty || !(*empty)->update({}))
    return 2;
  int first_count{}, second_count{};
  auto counter = [](int& output) {
    return [&output, count = 0](world&, const frame_time&) mutable { output = ++count; };
  };
  (*a)->add_system("counter", counter(first_count));
  (*b)->add_system("counter", counter(second_count));
  auto one = (*a)->create_scene(), two = (*a)->create_scene();
  if (!one || !two)
    return 3;
  auto object = two->create_object();
  if (!object)
    return 4;
  (*a)->add_system("name",
                   [id = object->entity()](world& target, const frame_time&) -> result<void> {
                     target.find(id).set_name("updated");
                     return {};
                   });
  if (!(*a)->update({}) || !(*a)->update({}) || !(*b)->update({}) || first_count != 2 ||
      second_count != 1 || object->name() != "updated")
    return 5;
  one->unload();
  if (!(*a)->update({}) || first_count != 3 || !object->is_valid() || second_count != 1)
    return 6;
  // 系统捕获先于 ECS 释放，另一个 World 不受销毁影响。
  bool released_before_objects{};
  auto life = create_native();
  if (!life)
    return 7;
  auto scene = (*life)->create_scene();
  if (!scene)
    return 8;
  auto living = scene->create_object();
  if (!living)
    return 9;
  auto token = std::shared_ptr<int>(new int, [&, handle = *living](int* ptr) {
    released_before_objects = handle.is_valid();
    delete ptr;
  });
  std::weak_ptr<int> weak = token;
  (*life)->add_system("retain", [token](world&, const frame_time&) {});
  token.reset();
  life->reset();
  if (!weak.expired() || !released_before_objects || !(*b)->update({}) || second_count != 2)
    return 10;

  auto failing = create_native();
  if (!failing)
    return 11;
  std::vector<int> order;
  (*failing)->add_system("first", [&](world&, const frame_time& time) {
    order.push_back(static_cast<int>(time.frame_index));
  });
  (*failing)->add_system("failure", [&](world&, const frame_time&) -> error {
    order.push_back(2);
    return error::not_found;
  });
  (*failing)->add_system("skipped", [&](world&, const frame_time&) { order.push_back(3); });
  auto failed = (*failing)->update({.frame_index = 1});
  if (failed || failed.error() != error::not_found || order != std::vector<int>{1, 2})
    return 12;
  // 异常向宿主传播，执行标记必须解除，不能把下次更新误判成重入。
  auto throwing = create_native();
  if (!throwing)
    return 13;
  (*throwing)->add_system("throw_once", [first = true](world&, const frame_time&) mutable {
    if (first) {
      first = false;
      throw std::runtime_error("expected");
    }
  });
  try {
    (void)(*throwing)->update({});
    return 14;
  } catch (const std::runtime_error&) {
  }
  if (!(*throwing)->update({}))
    return 15;
  return 0;
}
