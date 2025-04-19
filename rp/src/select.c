#include "select.h"

static reset_callback_t __not_in_flash_func(reset_cb) = NULL;
static reset_callback_t __not_in_flash_func(reset_long_cb) =
    NULL;  // New long-press callback

void __not_in_flash_func(select_waitPush)() {
  DPRINTF("Waiting for SELECT button to be released\n");
  uint32_t press_duration = 0;
  while (select_detectPush()) {
    tight_loop_contents();
    sleep_ms(SELECT_LOOP_DELAY);
    press_duration += SELECT_LOOP_DELAY;
    if (press_duration >= SELECT_LONG_RESET) {
      break;
    }
  }
  DPRINTF("SELECT button released after %d ms\n", press_duration);
  if (press_duration >= SELECT_LONG_RESET) {
    if (reset_long_cb != NULL) {
      DPRINTF("Long press detected. Executing long reset callback\n");
      reset_long_cb();
    }
  } else {
    if (reset_cb != NULL) {
      DPRINTF("Short press detected. Executing reset callback\n");
      reset_cb();
    }
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
  inline void core1_waitPush(void) {
    DPRINTF("Waiting for SELECT button to be pushed\n");
    // Wait until the SELECT button is pushed.
    while (!select_detectPush()) {
      tight_loop_contents();
      sleep_ms(SELECT_LOOP_DELAY);
    }
    DPRINTF("SELECT button pushed!\n");
    select_waitPush();
  }

  DPRINTF("Launching core 1 to wait for SELECT button push\n");
  reset_cb = reset;
  reset_long_cb = resetLong;
  multicore_launch_core1(core1_waitPush);
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