#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#include "timer.h"

#define LCD_COLS 16
#define LCD_ROWS 2
#define I2C_SDA 21
#define I2C_SCL 22

#define BTN_START_PIN 4
#define BTN_STOP_PIN 2

#define FINAL_BLINKS 3

LiquidCrystal_I2C* lcd = nullptr;
Timer timer;

uint32_t lastRefresh = 0;
uint32_t lastBlink = 0;
uint8_t blinkCount = 0;
bool displayOn = true;

String serialBuf;

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
  lcd->setCursor(0, 0);
  lcd->print("Elapsed Time:");
  writeTime(timer.elapsedMs());
}

void showFinal() {
  lcd->display();
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print("Final Time:");
  writeTime(timer.elapsedMs());
}

void startTimer() {
  if (timer.state() != Timer::RUNNING) {
    timer.start();
    displayOn = true;
    showElapsed();
    Serial.println("timer started");
  }
}

void stopTimer() {
  if (timer.state() == Timer::RUNNING) {
    timer.stop();
    displayOn = true;
    blinkCount = 0;
    lastBlink = millis();
    showFinal();
    Serial.println("timer stopped");
  }
}

void resetTimer() {
  timer.reset();
  displayOn = true;
  showElapsed();
  Serial.println("timer reset");
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
      } else {
        Serial.printf("unknown command: '%s' (expected: start | stop | reset)\n", serialBuf.c_str());
      }
      serialBuf = "";
    } else if (c != '\r') {
      serialBuf += c;
    }
  }
}

void handleButtons() {
  // Dummy handler - buttons not wired yet. Wire BTN_START_PIN and
  // BTN_STOP_PIN (active-low to GND) and call this from loop().
  static bool startPrev = HIGH;
  static bool stopPrev = HIGH;
  static uint32_t lastDebounce = 0;

  bool startNow = digitalRead(BTN_START_PIN);
  bool stopNow = digitalRead(BTN_STOP_PIN);
  if (startNow != startPrev || stopNow != stopPrev) {
    lastDebounce = millis();
    startPrev = startNow;
    stopPrev = stopNow;
  }
  if (millis() - lastDebounce < 50) return;
  if (startNow == LOW) startTimer();
  if (stopNow == LOW) stopTimer();
}

void updateTimeDisplay() {
  if (timer.state() == Timer::RUNNING && millis() - lastRefresh >= 50) {
    lastRefresh = millis();
    writeTime(timer.elapsedMs());
  }
}

void updateBlink() {
  if (timer.state() != Timer::STOPPED) return;
  if (blinkCount >= FINAL_BLINKS) return;
  if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    displayOn = !displayOn;
    if (displayOn) {
      lcd->display();
    } else {
      lcd->noDisplay();
      blinkCount++;
      if (blinkCount >= FINAL_BLINKS) {
        displayOn = true;
        lcd->display();
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(I2C_SDA, I2C_SCL);

  lcd = new LiquidCrystal_I2C(findLcdAddress(), LCD_COLS, LCD_ROWS);
  lcd->init();
  lcd->backlight();

  resetTimer();

  Serial.println("Timer ready. Commands: start | stop | reset");
}

void loop() {
  handleSerial();
  updateTimeDisplay();
  updateBlink();
}
