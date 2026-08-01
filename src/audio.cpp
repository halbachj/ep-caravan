#include "audio.h"

#include <LittleFS.h>
#include <BluetoothA2DPSource.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>

#define BT_SPEAKER_NAME "SPEAKER_NAME"
#define AUDIO_FILE "/theme.mp3"
#define BUFFER_SIZE 16384

class A2DPAudioOutput : public AudioOutput {
public:
  A2DPAudioOutput() {
    mutex_ = xSemaphoreCreateMutex();
  }

  bool begin() override {
    return true;
  }

  bool ConsumeSample(int16_t sample[2]) override {
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
      writeSample(sample[0]);
      writeSample(sample[1]);
      xSemaphoreGive(mutex_);
    }
    return true;
  }

  bool stop() override {
    return true;
  }

  size_t readData(uint8_t* data, size_t len) {
    size_t copied = 0;
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
      while (copied < len && count_ > 0) {
        data[copied++] = buffer_[readIdx_];
        readIdx_ = (readIdx_ + 1) % BUFFER_SIZE;
        count_--;
      }
      xSemaphoreGive(mutex_);
    }
    return copied;
  }

private:
  void writeSample(int16_t s) {
    if (count_ >= BUFFER_SIZE) {
      readIdx_ = (readIdx_ + 1) % BUFFER_SIZE;
      count_--;
    }
    buffer_[writeIdx_] = s & 0xFF;
    buffer_[(writeIdx_ + 1) % BUFFER_SIZE] = (s >> 8) & 0xFF;
    writeIdx_ = (writeIdx_ + 2) % BUFFER_SIZE;
    count_ += 2;
  }

  uint8_t buffer_[BUFFER_SIZE];
  volatile size_t readIdx_ = 0;
  volatile size_t writeIdx_ = 0;
  volatile size_t count_ = 0;
  SemaphoreHandle_t mutex_;
};

static BluetoothA2DPSource a2dp_source;
static AudioFileSourceLittleFS* file = nullptr;
static AudioGeneratorMP3* mp3 = nullptr;
static A2DPAudioOutput* out = nullptr;

static int32_t getSoundData(uint8_t* data, int32_t byteCount) {
  if (out == nullptr) {
    return 0;
  }
  return out->readData(data, byteCount);
}

void audioSetup() {
  if (!LittleFS.begin(true)) {
    Serial.println("audio: LittleFS mount failed");
    return;
  }

  out = new A2DPAudioOutput();
  file = new AudioFileSourceLittleFS(AUDIO_FILE);
  mp3 = new AudioGeneratorMP3();

  a2dp_source.set_data_callback(getSoundData);
  a2dp_source.start(BT_SPEAKER_NAME);

  Serial.printf("audio: starting playback of '%s', waiting for bluetooth connection...\n", AUDIO_FILE);
}

void audioLoop() {
  if (mp3 == nullptr || !a2dp_source.is_connected()) {
    return;
  }
  if (mp3->isRunning()) {
    if (!mp3->loop()) {
      mp3->stop();
      Serial.println("audio: playback finished");
    }
  }
}
