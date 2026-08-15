#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>

#include "timer.h"
#include "audio.h"

#define LCD_COLS 16
#define LCD_ROWS 2
#define I2C_SDA 21
#define I2C_SCL 22

#define BTN_ACTION_PIN 4
#define BTN_FINISH_PIN 5
#define BUTTON_DEBOUNCE_MS 50

#define LED_PIN 25
#define LED_COUNT 300

#define LASER_SWITCH_PIN 23
#define LASER_BOOT_TOGGLES 3
#define LASER_BOOT_TOGGLE_MS 250
#define LASER_BLINKS 3
#define LASER_BLINK_MS 250
#define LASER_DEBUG true

#define THEME_FILE "mission_impossible_theme.mp3"
#define ALARM_FILE "alarm.wav"
#define WIN_FILE "win_theme.mp3"
#define SIREN_DURATION_MS 5000
#define SIREN_BEAMS 6
#define SIREN_BEAM_WIDTH 18
#define SIREN_STEP_MS 15
#define SIREN_REFRESH_MS 30
#define LED_FLASHES 3
#define LED_FLASH_MS 250
#define RUNNING_RED_BRIGHTNESS 51

struct LaserInput {
  uint8_t pin;
  uint8_t number;
};

// GPIO34 and GPIO35 are input-only and have no internal pull-ups. Every
// sensor input therefore requires an external pull-up resistor.
const LaserInput LASERS[] = {
  {13, 1}, {12, 9}, {14, 8}, {27, 7}, {26, 6},
  {33, 5}, {32, 4}, {35, 3}, {34, 2},
};
const size_t LASER_COUNT = sizeof(LASERS) / sizeof(LASERS[0]);

enum GameState {
  READY,
  RUNNING,
  ALARM_BLINKING,
  ALARM_SIREN,
  ALARM_FLASHING,
  ALARM_LATCHED,
  VICTORY_BLINKING,
  VICTORY_SIREN,
  VICTORY_FLASHING,
  VICTORY_LATCHED,
};

LiquidCrystal_I2C* lcd = nullptr;
Timer timer;
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
GameState gameState = READY;

uint32_t lastRefresh = 0;
uint32_t stateChangedAt = 0;
uint8_t trippedLaser = 0;
bool laserStates[LASER_COUNT];
bool runningLedsShown = false;

String serialBuf;

struct ButtonState {
  uint8_t pin;
  bool reading;
  bool stable;
  uint32_t changedAt;
};

ButtonState actionButton = {BTN_ACTION_PIN, HIGH, HIGH, 0};
ButtonState finishButton = {BTN_FINISH_PIN, HIGH, HIGH, 0};

byte findLcdAddress() {
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C device found at 0x%02X\n", addr);
      return addr;
    }
  }
  Serial.println("WARNING: no I2C device found, defaulting to 0x27");
  return 0x27;
}

void writeTime(uint32_t ms) {
  uint16_t totalSec = ms / 1000;
  uint16_t mm = totalSec / 60;
  uint8_t ss = totalSec % 60;
  uint16_t mmm = ms % 1000;
  char buf[10];
  snprintf(buf, sizeof(buf), "%02u:%02u:%03u", mm, ss, mmm);
  lcd->setCursor(0, 1);
  lcd->print(buf);
}

void showElapsed() {
  lcd->display();
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print("Elapsed Time:");
  writeTime(timer.elapsedMs());
}

void showTripped() {
  lcd->display();
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print("LASER ");
  lcd->print(trippedLaser);
  lcd->print(" TRIPPED");
  writeTime(timer.elapsedMs());
}

void showWon() {
  lcd->display();
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print("WON!");
  writeTime(timer.elapsedMs());
}

void setLasers(bool enabled) {
  digitalWrite(LASER_SWITCH_PIN, enabled ? HIGH : LOW);
}

void setGameState(GameState nextState) {
  gameState = nextState;
  stateChangedAt = millis();
}

void clearLeds() {
  strip.clear();
  strip.show();
  runningLedsShown = false;
}

void startTimer() {
  if (gameState == READY) {
    timer.start();
    showElapsed();
    setLasers(true);
    setGameState(RUNNING);
    audioPlay(THEME_FILE);
    Serial.println("timer started");
  }
}

void stopTimer() {
  if (gameState == RUNNING) {
    timer.stop();
    setGameState(READY);
    showElapsed();
    clearLeds();
    audioStop();
    Serial.println("timer stopped");
  }
}

void resetTimer(bool force = false) {
  if (!force && digitalRead(BTN_FINISH_PIN) == LOW) {
    Serial.println("reset blocked: finish switch is pressed");
    return;
  }
  timer.reset();
  trippedLaser = 0;
  setLasers(true);
  setGameState(READY);
  showElapsed();
  clearLeds();
  audioStop();
  Serial.println("timer reset");
}

void finishGame() {
  if (gameState != RUNNING) return;

  timer.stop();
  setLasers(false);
  showWon();
  setGameState(VICTORY_BLINKING);
  audioPlayOnce(WIN_FILE);
  Serial.println("victory");
}

void printHelp() {
  Serial.println("Game:   start | stop | reset (GPIO4 action, GPIO5 finish)");
  Serial.println("BT:     list | status | connect <mac> | disconnect | auto <on|off>");
  Serial.println("Audio:  play <file>   (theme.mp3, alarm.wav, win_theme.mp3)");
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      serialBuf.trim();
      if (serialBuf == "start") {
        startTimer();
      } else if (serialBuf == "stop") {
        stopTimer();
      } else if (serialBuf == "reset") {
        resetTimer();
      } else if (serialBuf == "help") {
        printHelp();
      } else if (serialBuf == "list") {
        audioListDevices();
      } else if (serialBuf == "status") {
        audioStatus();
      } else if (serialBuf == "disconnect") {
        audioDisconnect();
      } else if (serialBuf == "scan") {
        Serial.println("Scanning runs continuously in the background; type 'list' to see results.");
      } else if (serialBuf.startsWith("connect ")) {
        audioConnectTo(serialBuf.substring(8).c_str());
      } else if (serialBuf == "auto on") {
        audioSetAutoReconnect(true);
      } else if (serialBuf == "auto off") {
        audioSetAutoReconnect(false);
      } else if (serialBuf.startsWith("play ")) {
        audioPlay(serialBuf.substring(5).c_str());
      } else {
        Serial.printf("unknown command: '%s' (type 'help')\n", serialBuf.c_str());
      }
      serialBuf = "";
    } else if (c != '\r') {
      serialBuf += c;
    }
  }
}

bool buttonPressed(ButtonState& button) {
  bool reading = digitalRead(button.pin);
  if (reading != button.reading) {
    button.reading = reading;
    button.changedAt = millis();
  }
  if (reading == button.stable || millis() - button.changedAt < BUTTON_DEBOUNCE_MS) {
    return false;
  }

  button.stable = reading;
  return button.stable == LOW;
}

void handleButtons() {
  if (buttonPressed(actionButton)) {
    if (gameState == READY) {
      startTimer();
    } else if (digitalRead(BTN_FINISH_PIN) == HIGH) {
      resetTimer();
    }
  }
  if (buttonPressed(finishButton)) {
    finishGame();
  }
}

void showRunningRed() {
  if (runningLedsShown) return;

  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(RUNNING_RED_BRIGHTNESS, 0, 0));
  }
  strip.show();
  runningLedsShown = true;
}

void showRotatingSiren(bool victory) {
  static uint32_t lastSirenLed = 0;
  if (millis() - lastSirenLed < SIREN_REFRESH_MS) return;
  lastSirenLed = millis();

  uint16_t head = (millis() / SIREN_STEP_MS) % LED_COUNT;
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    uint16_t nearest = LED_COUNT;
    for (uint8_t beam = 0; beam < SIREN_BEAMS; beam++) {
      uint16_t beamPosition = (head + beam * LED_COUNT / SIREN_BEAMS) % LED_COUNT;
      uint16_t distance = abs((int)i - (int)beamPosition);
      nearest = min(nearest, min(distance, (uint16_t)(LED_COUNT - distance)));
    }
    uint8_t brightness = nearest <= SIREN_BEAM_WIDTH
      ? 255 - nearest * (255 / SIREN_BEAM_WIDTH)
      : 0;
    strip.setPixelColor(i, victory ? strip.Color(0, brightness, 0)
                                   : strip.Color(brightness, 0, 0));
  }
  strip.show();
}

void showSolid(bool victory) {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, victory ? strip.Color(0, 255, 0) : strip.Color(255, 0, 0));
  }
  strip.show();
}

void tripLaser(uint8_t laserNumber) {
  trippedLaser = laserNumber;
  timer.stop();
  setLasers(false);
  showTripped();
  setGameState(ALARM_BLINKING);
  audioPlay(ALARM_FILE);
  Serial.printf("laser %u tripped\n", trippedLaser);
}

void updateLaserDebug() {
  if (!LASER_DEBUG) return;

  for (size_t i = 0; i < LASER_COUNT; i++) {
    bool state = digitalRead(LASERS[i].pin);
    if (state != laserStates[i]) {
      laserStates[i] = state;
      Serial.printf("laser %u: %s\n", LASERS[i].number, state ? "HIGH" : "LOW");
    }
  }
}

void checkLasers() {
  if (gameState != RUNNING) return;

  for (size_t i = 0; i < LASER_COUNT; i++) {
    if (digitalRead(LASERS[i].pin) == LOW) {
      tripLaser(LASERS[i].number);
      return;
    }
  }
}

void updateTimeDisplay() {
  if (gameState == RUNNING && millis() - lastRefresh >= 50) {
    lastRefresh = millis();
    writeTime(timer.elapsedMs());
  }
}

void updateAlarm() {
  if (gameState == ALARM_BLINKING) {
    uint32_t elapsed = millis() - stateChangedAt;
    uint8_t phase = elapsed / LASER_BLINK_MS;

    if (phase < LASER_BLINKS * 2) {
      setLasers((phase % 2) == 0);
      return;
    }

    setLasers(false);
    setGameState(ALARM_SIREN);
  }

  if (gameState == ALARM_SIREN) {
    showRotatingSiren(false);
    if (millis() - stateChangedAt >= SIREN_DURATION_MS) {
      audioStop();
      setGameState(ALARM_FLASHING);
    }
  }

  if (gameState == ALARM_FLASHING) {
    uint8_t phase = (millis() - stateChangedAt) / LED_FLASH_MS;
    if (phase < LED_FLASHES * 2) {
      if ((phase % 2) == 0) {
        showSolid(false);
      } else {
        clearLeds();
      }
    } else {
      showSolid(false);
      setGameState(ALARM_LATCHED);
    }
  }

  if (gameState == VICTORY_BLINKING) {
    uint8_t phase = (millis() - stateChangedAt) / LED_FLASH_MS;
    if (phase < LED_FLASHES * 2) {
      if ((phase % 2) == 0) {
        lcd->display();
      } else {
        lcd->noDisplay();
      }
    } else {
      showWon();
      setGameState(VICTORY_SIREN);
    }
  }

  if (gameState == VICTORY_SIREN) {
    showRotatingSiren(true);
    if (millis() - stateChangedAt >= SIREN_DURATION_MS) {
      setGameState(VICTORY_FLASHING);
    }
  }

  if (gameState == VICTORY_FLASHING) {
    uint8_t phase = (millis() - stateChangedAt) / LED_FLASH_MS;
    if (phase < LED_FLASHES * 2) {
      if ((phase % 2) == 0) {
        showSolid(true);
      } else {
        clearLeds();
      }
    } else {
      showSolid(true);
      setGameState(VICTORY_LATCHED);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  lcd = new LiquidCrystal_I2C(findLcdAddress(), LCD_COLS, LCD_ROWS);
  lcd->init();
  lcd->backlight();

  strip.begin();
  strip.setBrightness(64);
  strip.show();

  pinMode(LASER_SWITCH_PIN, OUTPUT);
  pinMode(BTN_ACTION_PIN, INPUT_PULLUP);
  pinMode(BTN_FINISH_PIN, INPUT_PULLUP);
  actionButton.reading = actionButton.stable = digitalRead(BTN_ACTION_PIN);
  finishButton.reading = finishButton.stable = digitalRead(BTN_FINISH_PIN);
  for (size_t i = 0; i < LASER_COUNT; i++) {
    pinMode(LASERS[i].pin, INPUT);
  }

  // GPIO12 uses its internal pull-up after boot; an external pull-up on
  // this boot-strapping pin would prevent reliable flash access.
  pinMode(12, INPUT_PULLUP);
  for (size_t i = 0; i < LASER_COUNT; i++) {
    laserStates[i] = digitalRead(LASERS[i].pin);
  }

  // Confirm laser-switch control at startup before leaving the emitters on.
  for (uint8_t i = 0; i < LASER_BOOT_TOGGLES; i++) {
    setLasers(true);
    delay(LASER_BOOT_TOGGLE_MS);
    setLasers(false);
    delay(LASER_BOOT_TOGGLE_MS);
  }

  resetTimer(true);

  Serial.println("Ready. Commands: start | stop | reset | help");

  audioSetup();
}

void loop() {
  handleSerial();
  handleButtons();
  updateLaserDebug();
  checkLasers();
  if (gameState == RUNNING) {
    showRunningRed();
  }
  updateTimeDisplay();
  updateAlarm();
  audioLoop();
}
