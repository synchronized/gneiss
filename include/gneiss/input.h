// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_INPUT_H_
#define GNEISS_INPUT_H_

#include <stddef.h>
#include <stdint.h>

#include <gneiss/application.h>
#include <gneiss/core/export.h>
#include <gneiss/core/result.h>

typedef uint32_t gneiss_physical_key;
#define GNEISS_PHYSICAL_KEY_UNKNOWN UINT32_C(0)
#define GNEISS_PHYSICAL_KEY_A UINT32_C(4)
#define GNEISS_PHYSICAL_KEY_B UINT32_C(5)
#define GNEISS_PHYSICAL_KEY_C UINT32_C(6)
#define GNEISS_PHYSICAL_KEY_D UINT32_C(7)
#define GNEISS_PHYSICAL_KEY_E UINT32_C(8)
#define GNEISS_PHYSICAL_KEY_F UINT32_C(9)
#define GNEISS_PHYSICAL_KEY_G UINT32_C(10)
#define GNEISS_PHYSICAL_KEY_H UINT32_C(11)
#define GNEISS_PHYSICAL_KEY_I UINT32_C(12)
#define GNEISS_PHYSICAL_KEY_J UINT32_C(13)
#define GNEISS_PHYSICAL_KEY_K UINT32_C(14)
#define GNEISS_PHYSICAL_KEY_L UINT32_C(15)
#define GNEISS_PHYSICAL_KEY_M UINT32_C(16)
#define GNEISS_PHYSICAL_KEY_N UINT32_C(17)
#define GNEISS_PHYSICAL_KEY_O UINT32_C(18)
#define GNEISS_PHYSICAL_KEY_P UINT32_C(19)
#define GNEISS_PHYSICAL_KEY_Q UINT32_C(20)
#define GNEISS_PHYSICAL_KEY_R UINT32_C(21)
#define GNEISS_PHYSICAL_KEY_S UINT32_C(22)
#define GNEISS_PHYSICAL_KEY_T UINT32_C(23)
#define GNEISS_PHYSICAL_KEY_U UINT32_C(24)
#define GNEISS_PHYSICAL_KEY_V UINT32_C(25)
#define GNEISS_PHYSICAL_KEY_W UINT32_C(26)
#define GNEISS_PHYSICAL_KEY_X UINT32_C(27)
#define GNEISS_PHYSICAL_KEY_Y UINT32_C(28)
#define GNEISS_PHYSICAL_KEY_Z UINT32_C(29)
#define GNEISS_PHYSICAL_KEY_1 UINT32_C(30)
#define GNEISS_PHYSICAL_KEY_2 UINT32_C(31)
#define GNEISS_PHYSICAL_KEY_3 UINT32_C(32)
#define GNEISS_PHYSICAL_KEY_4 UINT32_C(33)
#define GNEISS_PHYSICAL_KEY_5 UINT32_C(34)
#define GNEISS_PHYSICAL_KEY_6 UINT32_C(35)
#define GNEISS_PHYSICAL_KEY_7 UINT32_C(36)
#define GNEISS_PHYSICAL_KEY_8 UINT32_C(37)
#define GNEISS_PHYSICAL_KEY_9 UINT32_C(38)
#define GNEISS_PHYSICAL_KEY_0 UINT32_C(39)
#define GNEISS_PHYSICAL_KEY_ENTER UINT32_C(40)
#define GNEISS_PHYSICAL_KEY_ESCAPE UINT32_C(41)
#define GNEISS_PHYSICAL_KEY_BACKSPACE UINT32_C(42)
#define GNEISS_PHYSICAL_KEY_TAB UINT32_C(43)
#define GNEISS_PHYSICAL_KEY_SPACE UINT32_C(44)
#define GNEISS_PHYSICAL_KEY_F1 UINT32_C(58)
#define GNEISS_PHYSICAL_KEY_F2 UINT32_C(59)
#define GNEISS_PHYSICAL_KEY_F3 UINT32_C(60)
#define GNEISS_PHYSICAL_KEY_F4 UINT32_C(61)
#define GNEISS_PHYSICAL_KEY_F5 UINT32_C(62)
#define GNEISS_PHYSICAL_KEY_F6 UINT32_C(63)
#define GNEISS_PHYSICAL_KEY_F7 UINT32_C(64)
#define GNEISS_PHYSICAL_KEY_F8 UINT32_C(65)
#define GNEISS_PHYSICAL_KEY_F9 UINT32_C(66)
#define GNEISS_PHYSICAL_KEY_F10 UINT32_C(67)
#define GNEISS_PHYSICAL_KEY_F11 UINT32_C(68)
#define GNEISS_PHYSICAL_KEY_F12 UINT32_C(69)
#define GNEISS_PHYSICAL_KEY_INSERT UINT32_C(73)
#define GNEISS_PHYSICAL_KEY_HOME UINT32_C(74)
#define GNEISS_PHYSICAL_KEY_PAGE_UP UINT32_C(75)
#define GNEISS_PHYSICAL_KEY_DELETE UINT32_C(76)
#define GNEISS_PHYSICAL_KEY_END UINT32_C(77)
#define GNEISS_PHYSICAL_KEY_PAGE_DOWN UINT32_C(78)
#define GNEISS_PHYSICAL_KEY_RIGHT UINT32_C(79)
#define GNEISS_PHYSICAL_KEY_LEFT UINT32_C(80)
#define GNEISS_PHYSICAL_KEY_DOWN UINT32_C(81)
#define GNEISS_PHYSICAL_KEY_UP UINT32_C(82)
#define GNEISS_PHYSICAL_KEY_LEFT_CONTROL UINT32_C(224)
#define GNEISS_PHYSICAL_KEY_LEFT_SHIFT UINT32_C(225)
#define GNEISS_PHYSICAL_KEY_LEFT_ALT UINT32_C(226)
#define GNEISS_PHYSICAL_KEY_LEFT_SUPER UINT32_C(227)
#define GNEISS_PHYSICAL_KEY_RIGHT_CONTROL UINT32_C(228)
#define GNEISS_PHYSICAL_KEY_RIGHT_SHIFT UINT32_C(229)
#define GNEISS_PHYSICAL_KEY_RIGHT_ALT UINT32_C(230)
#define GNEISS_PHYSICAL_KEY_RIGHT_SUPER UINT32_C(231)

typedef uint32_t gneiss_logical_key;
#define GNEISS_LOGICAL_KEY_NONE UINT32_C(0)
#define GNEISS_LOGICAL_KEY_ENTER UINT32_C(1)
#define GNEISS_LOGICAL_KEY_ESCAPE UINT32_C(2)
#define GNEISS_LOGICAL_KEY_BACKSPACE UINT32_C(3)
#define GNEISS_LOGICAL_KEY_TAB UINT32_C(4)
#define GNEISS_LOGICAL_KEY_SPACE UINT32_C(5)
#define GNEISS_LOGICAL_KEY_LEFT UINT32_C(6)
#define GNEISS_LOGICAL_KEY_RIGHT UINT32_C(7)
#define GNEISS_LOGICAL_KEY_UP UINT32_C(8)
#define GNEISS_LOGICAL_KEY_DOWN UINT32_C(9)
#define GNEISS_LOGICAL_KEY_HOME UINT32_C(10)
#define GNEISS_LOGICAL_KEY_END UINT32_C(11)
#define GNEISS_LOGICAL_KEY_PAGE_UP UINT32_C(12)
#define GNEISS_LOGICAL_KEY_PAGE_DOWN UINT32_C(13)
#define GNEISS_LOGICAL_KEY_INSERT UINT32_C(14)
#define GNEISS_LOGICAL_KEY_DELETE UINT32_C(15)
#define GNEISS_LOGICAL_KEY_F1 UINT32_C(16)
#define GNEISS_LOGICAL_KEY_F2 UINT32_C(17)
#define GNEISS_LOGICAL_KEY_F3 UINT32_C(18)
#define GNEISS_LOGICAL_KEY_F4 UINT32_C(19)
#define GNEISS_LOGICAL_KEY_F5 UINT32_C(20)
#define GNEISS_LOGICAL_KEY_F6 UINT32_C(21)
#define GNEISS_LOGICAL_KEY_F7 UINT32_C(22)
#define GNEISS_LOGICAL_KEY_F8 UINT32_C(23)
#define GNEISS_LOGICAL_KEY_F9 UINT32_C(24)
#define GNEISS_LOGICAL_KEY_F10 UINT32_C(25)
#define GNEISS_LOGICAL_KEY_F11 UINT32_C(26)
#define GNEISS_LOGICAL_KEY_F12 UINT32_C(27)

typedef uint32_t gneiss_input_event_type;
#define GNEISS_INPUT_EVENT_KEY UINT32_C(1)
#define GNEISS_INPUT_EVENT_TEXT UINT32_C(2)
#define GNEISS_INPUT_EVENT_POINTER_MOVED UINT32_C(3)
#define GNEISS_INPUT_EVENT_POINTER_BUTTON UINT32_C(4)
#define GNEISS_INPUT_EVENT_POINTER_WHEEL UINT32_C(5)
#define GNEISS_INPUT_EVENT_POINTER_ENTERED UINT32_C(6)
#define GNEISS_INPUT_EVENT_POINTER_LEFT UINT32_C(7)

typedef uint32_t gneiss_key_action;
#define GNEISS_KEY_RELEASED UINT32_C(0)
#define GNEISS_KEY_PRESSED UINT32_C(1)
#define GNEISS_KEY_REPEATED UINT32_C(2)

#define GNEISS_MODIFIER_LEFT_SHIFT_BIT (UINT32_C(1) << 0)
#define GNEISS_MODIFIER_RIGHT_SHIFT_BIT (UINT32_C(1) << 1)
#define GNEISS_MODIFIER_LEFT_CONTROL_BIT (UINT32_C(1) << 2)
#define GNEISS_MODIFIER_RIGHT_CONTROL_BIT (UINT32_C(1) << 3)
#define GNEISS_MODIFIER_LEFT_ALT_BIT (UINT32_C(1) << 4)
#define GNEISS_MODIFIER_RIGHT_ALT_BIT (UINT32_C(1) << 5)
#define GNEISS_MODIFIER_LEFT_SUPER_BIT (UINT32_C(1) << 6)
#define GNEISS_MODIFIER_RIGHT_SUPER_BIT (UINT32_C(1) << 7)
#define GNEISS_MODIFIER_CAPS_LOCK_BIT (UINT32_C(1) << 8)
#define GNEISS_MODIFIER_NUM_LOCK_BIT (UINT32_C(1) << 9)

#define GNEISS_POINTER_PRIMARY_BIT (UINT32_C(1) << 0)
#define GNEISS_POINTER_SECONDARY_BIT (UINT32_C(1) << 1)
#define GNEISS_POINTER_MIDDLE_BIT (UINT32_C(1) << 2)
#define GNEISS_POINTER_X1_BIT (UINT32_C(1) << 3)
#define GNEISS_POINTER_X2_BIT (UINT32_C(1) << 4)

#define GNEISS_INPUT_TEXT_CAPACITY UINT32_C(48)

typedef union gneiss_input_event_data {
  struct {
    uint32_t physical_key;
    uint32_t logical_key;
    uint32_t modifiers;
    uint32_t action;
  } key;
  struct {
    uint32_t length;
    char utf8[GNEISS_INPUT_TEXT_CAPACITY];
    uint8_t reserved[12];
  } text;
  struct {
    float x;
    float y;
    float delta_x;
    float delta_y;
    uint32_t buttons;
    uint32_t reserved[11];
  } pointer_moved;
  struct {
    float x;
    float y;
    uint32_t button;
    uint32_t pressed;
    uint32_t buttons;
    uint32_t reserved[11];
  } pointer_button;
  struct {
    float x;
    float y;
    float delta_x;
    float delta_y;
    uint32_t buttons;
    uint32_t reserved[11];
  } pointer_wheel;
  uint8_t reserved[64];
} gneiss_input_event_data;

typedef struct gneiss_input_event {
  uint32_t struct_size;
  uint32_t type;
  uint64_t window_id;
  uint64_t timestamp_ns;
  gneiss_input_event_data data;
} gneiss_input_event;

#define GNEISS_INPUT_EVENT_VERSION_1_SIZE                                                          \
  ((uint32_t)(offsetof(gneiss_input_event, data) + sizeof(gneiss_input_event_data)))
#define GNEISS_INPUT_EVENT_INIT                                                                    \
  {                                                                                                \
    (uint32_t)sizeof(gneiss_input_event), UINT32_C(0), UINT64_C(0), UINT64_C(0), {                 \
      .reserved = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,               \
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,               \
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}                     \
    }                                                                                              \
  }

typedef struct gneiss_keyboard_state {
  uint32_t struct_size;
  uint32_t modifiers;
  uint64_t pressed_keys[4];
  uint64_t reserved[3];
} gneiss_keyboard_state;

#define GNEISS_KEYBOARD_STATE_VERSION_1_SIZE ((uint32_t)offsetof(gneiss_keyboard_state, reserved))
#define GNEISS_KEYBOARD_STATE_INIT                                                                 \
  {(uint32_t)sizeof(gneiss_keyboard_state), UINT32_C(0), {0, 0, 0, 0}, {0, 0, 0}}

typedef struct gneiss_pointer_state {
  uint32_t struct_size;
  uint32_t buttons;
  float x;
  float y;
  uint32_t is_inside;
  uint32_t reserved[5];
} gneiss_pointer_state;

#define GNEISS_POINTER_STATE_VERSION_1_SIZE ((uint32_t)offsetof(gneiss_pointer_state, reserved))
#define GNEISS_POINTER_STATE_INIT                                                                  \
  {(uint32_t)sizeof(gneiss_pointer_state), UINT32_C(0), 0.0F, 0.0F, UINT32_C(0), {0, 0, 0, 0, 0}}

#ifdef __cplusplus
extern "C" {
#endif

/** 取出当帧下一条输入事件；队列为空返回 GNEISS_ERROR_NOT_READY。 */
GNEISS_API gneiss_result gneiss_application_poll_input(gneiss_application application,
                                                       gneiss_input_event* out_event);

/** 返回当前帧键盘快照；仅允许在 Application 创建线程调用。 */
GNEISS_API gneiss_result gneiss_application_get_keyboard_state(gneiss_application application,
                                                               gneiss_keyboard_state* out_state);

/** 返回当前帧指针快照；仅允许在 Application 创建线程调用。 */
GNEISS_API gneiss_result gneiss_application_get_pointer_state(gneiss_application application,
                                                              gneiss_pointer_state* out_state);

#ifdef __cplusplus
}
#endif

#endif
