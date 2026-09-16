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

// UART link to the ESP32-C3 DS1302 RTC coprocessor, on the P3 header
// (115200 8N1, framed binary rtclink-common protocol). GPIO35 is input-only
// (C3 TX provides the idle-high level); never configure it OUTPUT or with a
// pull. This is a dedicated UART (Serial1), fully independent of the CH340
// console. The old P1/UART0 route (GPIO3/GPIO1) is rejected: the CH340 holds
// GPIO3 HIGH the moment the CYD 5V rail is powered.
constexpr gpio_num_t kRtcUartRx = GPIO_NUM_35;  // <- C3 GPIO21 (TXD)
constexpr gpio_num_t kRtcUartTx = GPIO_NUM_22;  // -> C3 GPIO20 (RXD)
constexpr uint32_t kRtcUartBaud = 115200;

}  // namespace pins
