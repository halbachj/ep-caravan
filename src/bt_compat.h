#pragma once

#include <Arduino.h>
#include <esp_bt.h>

#undef B0
#undef B1

#ifndef BT_MODE_NONE
#define BT_MODE_NONE ESP_BT_MODE_IDLE
#endif
#ifndef BT_MODE_BLE
#define BT_MODE_BLE ESP_BT_MODE_BLE
#endif
#ifndef BT_MODE_CLASSIC_BT
#define BT_MODE_CLASSIC_BT ESP_BT_MODE_CLASSIC_BT
#endif
#ifndef BT_MODE_BTDM
#define BT_MODE_BTDM ESP_BT_MODE_BTDM
#endif

inline bool btStartMode(esp_bt_mode_t mode) {
  (void)mode;
  return btStart();
}
