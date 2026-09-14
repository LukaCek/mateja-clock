#ifdef MATEJA_AUDIO_LAB

#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include <SPI.h>

namespace {

constexpr uint8_t kSdChipSelect = 5;
constexpr uint8_t kSdClock = 18;
constexpr uint8_t kSdMiso = 19;
constexpr uint8_t kSdMosi = 23;
constexpr char kAlarmPath[] = "/clock/audio/alarm.wav";

SPIClass sdSpi(VSPI);
Audio audio(true, I2S_DAC_CHANNEL_LEFT_EN);

}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Mateja Clock isolated ESP32-audioI2S lab");
  sdSpi.begin(kSdClock, kSdMiso, kSdMosi, kSdChipSelect);
  if (!SD.begin(kSdChipSelect, sdSpi, 10000000)) {
    Serial.println("AUDIO_LAB sd=fail");
    return;
  }
  Serial.printf("AUDIO_LAB sd=ready file=%s bytes=%u\n", kAlarmPath,
                static_cast<unsigned>(SD.open(kAlarmPath).size()));
  audio.forceMono(true);
  audio.setVolume(12);
  const bool started = audio.connecttoFS(SD, kAlarmPath);
  Serial.printf("AUDIO_LAB start=%s volume=12\n", started ? "yes" : "no");
}

void loop() {
  audio.loop();
  delay(1);
}

void audio_info(const char* info) {
  Serial.print("AUDIO_LAB info=");
  Serial.println(info);
}

void audio_eof_mp3(const char*) {
  Serial.println("AUDIO_LAB eof; restarting");
  audio.connecttoFS(SD, kAlarmPath);
}

#endif
