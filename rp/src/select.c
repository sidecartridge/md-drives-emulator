#include "select.h"

static reset_callback_t __not_in_flash_func(reset_cb) = NULL;
static reset_callback_t __not_in_flash_func(reset_long_cb) = NULL;

static bool __not_in_flash_func(select_is_stable_state)(bool pressed_state) {
  uint32_t stable_ms = 0;

  while (stable_ms < SELECT_DEBOUNCE_MS) {
    if (select_detectPush() != pressed_state) {
      return false;
    }

    tight_loop_contents();
    sleep_ms(SELECT_LOOP_DELAY);
    stable_ms += SELECT_LOOP_DELAY;
  }

  return true;
}

static void __not_in_flash_func(select_wait_for_press_loop)(void) {
  while (true) {
    DPRINTF("Waiting for SELECT button to be pushed\n");
    while (!select_detectPush()) {
      tight_loop_contents();
      sleep_ms(SELECT_LOOP_DELAY);
    }

    if (!select_is_stable_state(true)) {
      DPRINTF("Ignoring unstable SELECT press\n");
      continue;
    }

    DPRINTF("SELECT button pushed!\n");
    select_waitPush();
  }
}

void __not_in_flash_func(select_waitPush)() {
  DPRINTF("Waiting for SELECT button to be released\n");
  uint64_t press_start_us = time_us_64();
  bool long_press_handled = false;

  while (true) {
    tight_loop_contents();
    sleep_ms(SELECT_LOOP_DELAY);

    if (select_detectPush()) {
      if (!long_press_handled &&
          (time_us_64() - press_start_us >=
           ((uint64_t)SELECT_LONG_RESET * 1000ULL))) {
        DPRINTF("Long press detected. Executing long reset callback\n");
        long_press_handled = true;
        if (reset_long_cb != NULL) {
          reset_long_cb();
        }
      }
      continue;
    }

    if (!select_is_stable_state(false)) {
      continue;
    }

    DPRINTF("SELECT button released after %llu ms\n",
            (unsigned long long)((time_us_64() - press_start_us) / 1000ULL));
    if (!long_press_handled && reset_cb != NULL) {
      DPRINTF("Short press detected. Executing reset callback\n");
      reset_cb();
    }
      break;
  }

  DPRINTF("SELECT button callback returned!\n");
}

void select_configure() {
  // Configure the input ping for SELECT button
  gpio_init(SELECT_GPIO);
  gpio_set_dir(SELECT_GPIO, GPIO_IN);
  gpio_set_pulls(SELECT_GPIO, false, true);  // Pull down (false, true)
  gpio_pull_down(SELECT_GPIO);
}

bool select_detectPush() { return (gpio_get(SELECT_GPIO) != 0); }

void select_coreWaitPush(reset_callback_t reset, reset_callback_t resetLong) {
  DPRINTF("Launching core 1 to wait for SELECT button push\n");
  reset_cb = reset;
  reset_long_cb = resetLong;
  multicore_launch_core1(select_wait_for_press_loop);
}

void select_coreWaitPushDisable() {
  DPRINTF("Disabling core 1\n");
  multicore_reset_core1();
}

void select_checkPushReset() {
  if (select_detectPush()) {
    DPRINTF("SELECT button pushed. Resetting the device\n");
    if (reset_cb != NULL) {
      DPRINTF("SELECT button pushed. Executing reset callback\n");
      reset_cb();
    }
  }
}

void select_setResetCallback(reset_callback_t reset) { reset_cb = reset; }
void select_setLongResetCallback(reset_callback_t resetLong) {
  reset_long_cb = resetLong;
}
