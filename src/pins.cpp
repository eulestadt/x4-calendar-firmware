#include <Arduino.h>
#include <config.h>
#include <x4_sleep.h>
#ifndef BOARD_TRMNL_X
extern TRMNL_DEVICE *pDevice;
#endif

void pins_init(void) {
#ifdef BOARD_XTEINK_X4
  // Holds from the previous sleep survive wakeup and would freeze SPI.
  x4_release_sleep_holds();
#endif
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
#ifdef BOARD_TRMNL_X
  io_conf.pin_bit_mask = (1ULL << PIN_INTERRUPT);
#else
  io_conf.pin_bit_mask = (1ULL << pDevice->interrupt_pin);
#endif
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io_conf);
}
