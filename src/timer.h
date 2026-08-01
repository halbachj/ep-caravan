#pragma once

#include <Arduino.h>

class Timer {
public:
  enum State { IDLE, RUNNING, STOPPED };

  void start();
  void stop();
  void reset();
  uint32_t elapsedMs() const;
  State state() const;

private:
  State state_ = IDLE;
  uint32_t runStart_ = 0;
  uint32_t elapsedAccum_ = 0;
};
