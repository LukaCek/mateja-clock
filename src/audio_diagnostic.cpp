#ifdef MATEJA_AUDIO_DIAGNOSTIC

#include <Arduino.h>

namespace {

constexpr uint8_t kAudioPin = 26;
constexpr uint8_t kAudioChannel = 0;

}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Mateja Clock isolated GPIO26 audio diagnostic");
  const uint32_t setup = ledcSetup(kAudioChannel, 880, 10);
  ledcAttachPin(kAudioPin, kAudioChannel);
  const uint32_t tone = ledcWriteTone(kAudioChannel, 880);
  Serial.printf("AUDIO pin=26 channel=0 setup=%u tone=%u read=%u\n", setup,
                tone, ledcReadFreq(kAudioChannel));
}

void loop() {
  delay(1000);
}

#endif
