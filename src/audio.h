#pragma once

#include <Arduino.h>

void audioSetup();
void audioLoop();
void audioListDevices();
void audioStatus();
bool audioConnectTo(const char* mac);
void audioDisconnect();
void audioSetAutoReconnect(bool enable);
bool audioPlay(const char* file);
bool audioPlayOnce(const char* file);
void audioStop();
