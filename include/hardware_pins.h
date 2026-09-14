#pragma once

#include <Arduino.h>

namespace pins {

// Verified ESP32-2432S028 two-USB ST7789 pinout.
constexpr gpio_num_t kTftMiso = GPIO_NUM_12;
constexpr gpio_num_t kTftMosi = GPIO_NUM_13;
constexpr gpio_num_t kTftClock = GPIO_NUM_14;
constexpr gpio_num_t kTftChipSelect = GPIO_NUM_15;
constexpr gpio_num_t kTftDataCommand = GPIO_NUM_2;
constexpr int8_t kTftReset = -1;

constexpr gpio_num_t kTouchMiso = GPIO_NUM_39;
constexpr gpio_num_t kTouchMosi = GPIO_NUM_32;
constexpr gpio_num_t kTouchClock = GPIO_NUM_25;
constexpr gpio_num_t kTouchChipSelect = GPIO_NUM_33;
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_36;

constexpr gpio_num_t kSdMiso = GPIO_NUM_19;
constexpr gpio_num_t kSdMosi = GPIO_NUM_23;
constexpr gpio_num_t kSdClock = GPIO_NUM_18;
constexpr gpio_num_t kSdChipSelect = GPIO_NUM_5;

constexpr gpio_num_t kBacklight = GPIO_NUM_21;
constexpr gpio_num_t kLightSensor = GPIO_NUM_34;
constexpr gpio_num_t kAudio = GPIO_NUM_26;

}  // namespace pins
