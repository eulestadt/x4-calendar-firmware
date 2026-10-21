#include <Arduino.h>
#include <config.h>

#if defined(BOARD_XTEINK_X4) && !defined(X4_WAKE_ON_BUTTON)

#include <x4_sleep.h>

#include "driver/gpio.h"
#if ESP_ARDUINO_VERSION_MAJOR < 3
#include <driver/adc.h>
#endif

// Pins from the xteink_x4 row in display.cpp, plus the SD bus from the board schematic.
// GPIO8 is a strapping pin, but with GPIO9 left high it does not select the ROM bootloader.
static constexpr gpio_num_t PIN_EPD_SCK = GPIO_NUM_8;
static constexpr gpio_num_t PIN_EPD_MOSI = GPIO_NUM_10;
static constexpr gpio_num_t PIN_EPD_CS = GPIO_NUM_21;
static constexpr gpio_num_t PIN_EPD_DC = GPIO_NUM_4;
static constexpr gpio_num_t PIN_EPD_RST = GPIO_NUM_5;
static constexpr gpio_num_t PIN_EPD_BUSY = GPIO_NUM_6;
static constexpr gpio_num_t PIN_SD_MISO = GPIO_NUM_7;
static constexpr gpio_num_t PIN_SD_CS = GPIO_NUM_12;
static constexpr gpio_num_t PIN_BATTERY_LATCH = GPIO_NUM_13;

static constexpr gpio_num_t kHeldPins[] = {
    PIN_EPD_SCK, PIN_EPD_MOSI, PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_SD_CS,
};

// Mid-level voltages on these pins sit in the input-buffer linear region.
// GPIO0 is the battery divider (~Vbat/2). GPIO20 is USB-present (~1 V on battery).
// GPIO1 and GPIO2 are the button ladders.
static constexpr gpio_num_t kParkedPins[] = {
    GPIO_NUM_0, GPIO_NUM_1,  GPIO_NUM_2,  PIN_EPD_BUSY, PIN_SD_MISO, GPIO_NUM_11,
    GPIO_NUM_14, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_20,
};

static void hold_level(gpio_num_t pin, int level) {
  gpio_sleep_sel_dis(pin);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  gpio_set_level(pin, level);
  gpio_hold_en(pin);
}

static void park_pin(gpio_num_t pin) {
  gpio_hold_dis(pin);
  gpio_sleep_sel_dis(pin);
  gpio_set_pull_mode(pin, GPIO_FLOATING);
  gpio_set_direction(pin, GPIO_MODE_DISABLE);
  gpio_sleep_set_pull_mode(pin, GPIO_FLOATING);
  gpio_sleep_set_direction(pin, GPIO_MODE_DISABLE);
  gpio_sleep_sel_en(pin);
}

static uint8_t sd_xfer(uint8_t out) {
  uint8_t in = 0;
  for (int bit = 7; bit >= 0; --bit) {
    gpio_set_level(PIN_EPD_MOSI, (out >> bit) & 1);
    delayMicroseconds(2);
    gpio_set_level(PIN_EPD_SCK, 1);
    delayMicroseconds(2);
    in = (uint8_t)((in << 1) | (gpio_get_level(PIN_SD_MISO) ? 1 : 0));
    gpio_set_level(PIN_EPD_SCK, 0);
  }
  return in;
}

static int sd_command(uint8_t cmd, uint32_t arg, uint8_t crc) {
  gpio_set_level(PIN_SD_CS, 0);
  sd_xfer(0x40 | cmd);
  sd_xfer((uint8_t)(arg >> 24));
  sd_xfer((uint8_t)(arg >> 16));
  sd_xfer((uint8_t)(arg >> 8));
  sd_xfer((uint8_t)arg);
  sd_xfer(crc);
  for (int i = 0; i < 8; ++i) {
    uint8_t response = sd_xfer(0xFF);
    if ((response & 0x80) == 0) {
      return response;
    }
  }
  return -1;
}

// Put the card in standby. TRMNL never mounts it, so a card left after power-up
// can sit selected and draw milliamps. Missing card: CMD0 times out and we stop.
static void sd_standby(void) {
  gpio_set_direction(PIN_EPD_SCK, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_EPD_MOSI, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_EPD_CS, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_SD_CS, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_SD_MISO, GPIO_MODE_INPUT);
  gpio_set_level(PIN_EPD_CS, 1);
  gpio_set_level(PIN_SD_CS, 1);
  gpio_set_level(PIN_EPD_SCK, 0);
  gpio_set_level(PIN_EPD_MOSI, 1);

  for (int i = 0; i < 10; ++i) {
    sd_xfer(0xFF);
  }

  bool idle = false;
  for (int attempt = 0; attempt < 8 && !idle; ++attempt) {
    int response = sd_command(0, 0, 0x95);
    gpio_set_level(PIN_SD_CS, 1);
    sd_xfer(0xFF);
    idle = (response == 0x01);
  }
  if (!idle) {
    gpio_set_level(PIN_SD_CS, 1);
    return;
  }

  sd_command(8, 0x1AA, 0x87);
  gpio_set_level(PIN_SD_CS, 1);
  sd_xfer(0xFF);

  const uint32_t deadline = millis() + 200;
  do {
    sd_command(55, 0, 0xFF);
    gpio_set_level(PIN_SD_CS, 1);
    sd_xfer(0xFF);
    int response = sd_command(41, 0x40000000, 0xFF);
    gpio_set_level(PIN_SD_CS, 1);
    sd_xfer(0xFF);
    if (response == 0x00) {
      break;
    }
  } while ((int32_t)(deadline - millis()) > 0);

  gpio_set_level(PIN_SD_CS, 1);
  sd_xfer(0xFF);
}

void x4_release_sleep_holds(void) {
  // GPIO13 stays held. Releasing it drops the battery latch.
  gpio_deep_sleep_hold_dis();
  for (gpio_num_t pin : kHeldPins) {
    gpio_hold_dis(pin);
    gpio_sleep_sel_dis(pin);
  }
  for (gpio_num_t pin : kParkedPins) {
    gpio_hold_dis(pin);
    gpio_sleep_sel_dis(pin);
  }
}

void x4_prepare_timed_sleep(void) {
  // Caller has already sent the panel sleep command. A second one would pulse
  // reset and wake the controller. Reset has to stay high from here on, or the
  // controller leaves sleep and draws 1–2 mA.
  sd_standby();

  hold_level(PIN_EPD_RST, 1);
  hold_level(PIN_EPD_CS, 1);
  hold_level(PIN_SD_CS, 1);
  hold_level(PIN_EPD_DC, 0);
  hold_level(PIN_EPD_MOSI, 0);
  hold_level(PIN_EPD_SCK, 0);

  for (gpio_num_t pin : kParkedPins) {
    park_pin(pin);
  }

#if ESP_ARDUINO_VERSION_MAJOR < 3
  adc_power_release();
#endif

  // Latch the level the board already biases onto GPIO13. Do not drive it.
  gpio_hold_en(PIN_BATTERY_LATCH);
  gpio_deep_sleep_hold_en();
}

#else

void x4_release_sleep_holds(void) {}
void x4_prepare_timed_sleep(void) {}

#endif
