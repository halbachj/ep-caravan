#include "audio.h"

#include <LittleFS.h>
#include <BluetoothA2DPSource.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutput.h>

#include <stdio.h>
#include <string.h>

#define BT_SPEAKER_NAME "SPEAKER_NAME"
#define AUDIO_FILE "/mission_impossible_theme.mp3"
#define BUFFER_SIZE 32768
#define MAX_DEVICES 16

typedef struct {
  char name[33];
  uint8_t bda[6];
  int8_t rssi;
  bool valid;
} bt_device_t;

static bt_device_t devices[MAX_DEVICES];
static uint8_t deviceCount = 0;
static bool autoconnect = false;

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
      if (count_ + 4 > BUFFER_SIZE) {
        xSemaphoreGive(mutex_);
        return false;
      }
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
static AudioGeneratorWAV* wav = nullptr;
static AudioGenerator* gen = nullptr;
static A2DPAudioOutput* out = nullptr;

static String currentFile = AUDIO_FILE;

static AudioGenerator* selectGenerator(const char* path) {
  if (strstr(path, ".wav") != nullptr || strstr(path, ".WAV") != nullptr) {
    return wav;
  }
  return mp3;
}

static int32_t getSoundData(uint8_t* data, int32_t byteCount) {
  if (out == nullptr) {
    return 0;
  }
  return out->readData(data, byteCount);
}

static int findDevice(const uint8_t* bda) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].valid && memcmp(devices[i].bda, bda, 6) == 0) {
      return i;
    }
  }
  return -1;
}

static void clearDevices() {
  deviceCount = 0;
  for (int i = 0; i < MAX_DEVICES; i++) {
    devices[i].valid = false;
  }
}

// Discovery callback: collect compatible speakers, never auto-connect.
static bool scanCallback(const char* name, esp_bd_addr_t address, int rssi) {
  int idx = findDevice(address);
  if (idx < 0) {
    if (deviceCount >= MAX_DEVICES) {
      return false;
    }
    idx = deviceCount++;
    devices[idx].valid = true;
    memcpy(devices[idx].bda, address, ESP_BD_ADDR_LEN);
  }
  strncpy(devices[idx].name, name ? name : "?", sizeof(devices[idx].name) - 1);
  devices[idx].name[sizeof(devices[idx].name) - 1] = 0;
  devices[idx].rssi = rssi;
  return false;
}

static bool parseMac(const char* mac, uint8_t bda[6]) {
  int b[6];
  if (sscanf(mac, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4],
             &b[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; i++) {
    bda[i] = b[i];
  }
  return true;
}

void audioSetup() {
  if (!LittleFS.begin(true)) {
    Serial.println("audio: LittleFS mount failed");
    return;
  }

  out = new A2DPAudioOutput();
  file = new AudioFileSourceLittleFS(AUDIO_FILE);
  mp3 = new AudioGeneratorMP3();
  wav = new AudioGeneratorWAV();
  gen = mp3;

  a2dp_source.set_data_callback(getSoundData);
  a2dp_source.set_local_name(BT_SPEAKER_NAME);
  a2dp_source.set_ssid_callback(scanCallback);
  a2dp_source.set_auto_reconnect(false);
  a2dp_source.start();

  Serial.printf("audio: scanning for speakers, will play '%s' once connected.\n", AUDIO_FILE);
  Serial.println("       BT commands: list | status | connect <mac> | disconnect | auto <on|off>");
}

void audioLoop() {
  if (mp3 == nullptr || out == nullptr) {
    return;
  }
  if (!a2dp_source.is_connected()) {
    return;
  }
  gen = selectGenerator(currentFile.c_str());
  if (!gen->isRunning()) {
    if (!file->isOpen()) {
      file->open(currentFile.c_str());
    }
    file->seek(0, SEEK_SET);
    if (gen->begin(file, out)) {
      Serial.println("audio: playback started");
    } else {
      Serial.printf("audio: playback start failed (file %s, exists %d, free heap %u)\n",
                    file->isOpen() ? "open" : "closed",
                    LittleFS.exists(AUDIO_FILE),
                    ESP.getFreeHeap());
    }
    return;
  }
  if (!gen->loop()) {
    gen->stop();
    static uint32_t lastRestartLog = 0;
    if (millis() - lastRestartLog > 5000) {
      lastRestartLog = millis();
      Serial.println("audio: playback finished, restarting");
    }
  }
}

void audioListDevices() {
  if (deviceCount == 0) {
    Serial.println("No speakers found yet (scanning in the background).");
    return;
  }
  for (int i = 0; i < deviceCount; i++) {
    Serial.printf("%02d  %-32s  %02X:%02X:%02X:%02X:%02X:%02X  RSSI %d dBm\n",
                  i, devices[i].name, devices[i].bda[0], devices[i].bda[1],
                  devices[i].bda[2], devices[i].bda[3], devices[i].bda[4],
                  devices[i].bda[5], devices[i].rssi);
  }
  if (a2dp_source.is_discovery_active()) {
    Serial.println("Still scanning... (more devices may appear)");
  }
}

void audioStatus() {
  Serial.printf("Connected:      %s\n", a2dp_source.is_connected() ? "yes" : "no");
  Serial.printf("Scanning:       %s\n",
                a2dp_source.is_discovery_active() ? "yes" : "no");
  Serial.printf("Auto-reconnect: %s\n", autoconnect ? "on" : "off");
  Serial.printf("Devices seen:   %u\n", deviceCount);
}

bool audioConnectTo(const char* mac) {
  uint8_t bda[6];
  if (!parseMac(mac, bda)) {
    Serial.printf("Invalid MAC '%s' (use format AA:BB:CC:DD:EE:FF)\n", mac);
    return false;
  }
  if (a2dp_source.is_connected()) {
    a2dp_source.disconnect();
    delay(200);
  }
  a2dp_source.cancel_discovery();
  delay(200);
  Serial.printf("Connecting to %02X:%02X:%02X:%02X:%02X:%02X...\n", bda[0],
                bda[1], bda[2], bda[3], bda[4], bda[5]);
  return a2dp_source.connect_to(bda);
}

void audioDisconnect() {
  Serial.println("audio: disconnecting");
  a2dp_source.disconnect();
}

void audioSetAutoReconnect(bool enable) {
  autoconnect = enable;
  a2dp_source.set_auto_reconnect(enable);
  Serial.printf("audio: auto-reconnect %s\n", enable ? "on" : "off");
}

bool audioPlay(const char* name) {
  currentFile = "/";
  currentFile += name;
  if (gen != nullptr && gen->isRunning()) {
    gen->stop();
  }
  if (!LittleFS.exists(currentFile.c_str())) {
    Serial.printf("audio: '%s' not on filesystem. Available files:\n", currentFile.c_str());
    File dir = LittleFS.open("/");
    File f = dir.openNextFile();
    while (f) {
      Serial.printf("  %s\n", f.name());
      f = dir.openNextFile();
    }
    dir.close();
    return false;
  }
  gen = selectGenerator(currentFile.c_str());
  file->open(currentFile.c_str());
  Serial.printf("audio: will play '%s'\n", currentFile.c_str());
  return true;
}
