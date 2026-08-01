#include "timer.h"

void Timer::start() {
  if (state_ != RUNNING) {
    runStart_ = millis();
    state_ = RUNNING;
  }
}

void Timer::stop() {
  if (state_ == RUNNING) {
    elapsedAccum_ += (millis() - runStart_);
    state_ = STOPPED;
  }
}

void Timer::reset() {
  elapsedAccum_ = 0;
  state_ = IDLE;
}

uint32_t Timer::elapsedMs() const {
  if (state_ == RUNNING) {
    return elapsedAccum_ + (millis() - runStart_);
  }
  return elapsedAccum_;
}

Timer::State Timer::state() const {
  return state_;
}
