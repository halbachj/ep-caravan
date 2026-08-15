# EP Caravan

ESP32 laser-trip game and alarm system. A running game monitors nine
active-low infrared trip wires, records the completion time, drives a 300-pixel
WS2812B strip, and streams game audio to a Bluetooth speaker.

See [the game state machine](docs/game-state-machine.md) for the complete
transition reference.

## Development

The project uses PlatformIO with an ESP32 DevKit V1 target. The included Nix
flake provides PlatformIO, Python with pyserial, and git.

```sh
direnv allow               # optional, enables the Nix environment automatically
nix develop                # alternative to direnv
pio run                    # build firmware
pio run -t buildfs         # build LittleFS media image
pio run -t upload          # upload firmware
pio run -t uploadfs        # upload LittleFS media image
```

Upload both firmware and LittleFS after changing code and media. The configured
port is `/dev/ttyUSB0`; Linux may assign the board `/dev/ttyUSB1` after a
reconnect. Use a temporary override without editing `platformio.ini`:

```sh
pio run -t upload --upload-port /dev/ttyUSB1
pio run -t uploadfs --upload-port /dev/ttyUSB1
```

Open a timestamped serial monitor with:

```sh
pio device monitor -b 115200 -f time
```

## Hardware

All signal grounds, the ESP32, the laser receiver circuitry, LCD, LED power
supply, and Bluetooth power source must share a common ground.

| Function | ESP32 pin | Notes |
| --- | --- | --- |
| WS2812B data | GPIO25 | 300 LEDs, GRB order |
| Laser power switch | GPIO23 | HIGH enables lasers; boot cycles it three times |
| Action button | GPIO4 (`D4`) | Active-low, internal pull-up |
| Finish switch | GPIO5 (`D5`) | Active-low, internal pull-up toggle switch |
| LCD SDA | GPIO21 | I2C, 100 kHz |
| LCD SCL | GPIO22 | I2C, 100 kHz |
| Laser 1 | GPIO13 | Active-low receiver input |
| Laser 9 | GPIO12 | Uses ESP32 internal pull-up only |
| Laser 8 | GPIO14 | Active-low receiver input |
| Laser 7 | GPIO27 | Active-low receiver input |
| Laser 6 | GPIO26 | Active-low receiver input |
| Laser 5 | GPIO33 | Active-low receiver input |
| Laser 4 | GPIO32 | Active-low receiver input |
| Laser 3 | GPIO35 | Active-low receiver input |
| Laser 2 | GPIO34 | Active-low receiver input |

### Laser inputs

Each receiver is normally HIGH and pulls its GPIO LOW when its beam breaks.
Use a 10 kOhm external pull-up from each receiver signal to `3V3`, except for
GPIO12. GPIO12 is a boot-strapping pin: an external pull-up can force the
ESP32 flash supply to 1.8 V at reset and prevent booting or uploads. GPIO12 is
configured as `INPUT_PULLUP` after boot instead.

GPIO34 and GPIO35 are input-only and have no internal pull-ups, so their
external resistors are mandatory. Do not apply 5 V to any ESP32 GPIO.

### Buttons

Wire each button or switch between its GPIO and GND. Internal pull-ups hold the
inputs HIGH when open. Inputs are debounced for 50 ms.

- GPIO4 starts a game from `READY`.
- GPIO4 resets every non-ready state.
- GPIO5 finishes a running game. It is a toggle switch; reset is blocked while
  it remains pressed/LOW, including the serial `reset` command.

### LED strip

The strip is capped at a global brightness of `64/255`. A 300-pixel strip can
draw far more current than USB can supply, so power it from a dedicated 5 V
supply and connect that supply ground to ESP32 GND.

The running display is a static dim red state. Alarm and victory animations
update at 30 fps to leave CPU time for Bluetooth audio.

### LCD and I2C

The LCD is an HD44780-compatible 16x2 display with a PCF8574 backpack. The
firmware scans for its address at boot and defaults to `0x27` if no device is
found. I2C runs at 100 kHz.

Keep I2C wiring short where possible. For a longer run, twist `SDA+GND` and
`SCL+GND`, keep those pairs away from LED power wires, and use an I2C level
shifter. A 5 V-powered backpack often pulls SDA/SCL to 5 V, which exceeds the
ESP32 GPIO rating; do not rely on the ESP32's clamp diodes as level shifting.

At 10 kHz this LCD library takes about 109 ms to write the timer value, which
starves audio playback. Do not lower the bus below 100 kHz without redesigning
the display update path.

## Game operation

At boot, GPIO23 cycles laser power HIGH/LOW three times at 250 ms per phase,
then lasers are enabled. The LCD starts at `Elapsed Time:` with a zeroed timer.

- Press GPIO4 or send `start` to begin.
- Break any laser while running to record a loss and its laser number.
- Toggle GPIO5 LOW to finish and record a win.
- Press GPIO4 or send `reset` to return to `READY`, unless GPIO5 is still LOW.
- Send `stop` to stop a running game and return to `READY`.

`LASER_DEBUG` is currently enabled in `src/main.cpp`; every input transition is
printed as `laser <number>: HIGH` or `laser <number>: LOW`.

## Bluetooth audio

The ESP32 is an A2DP source. Set `BT_SPEAKER_NAME` in `src/audio.cpp` to the
desired advertised name. Pair the speaker and send `connect <mac>` once; its
MAC address is stored in ESP32 NVS and automatically restored after reboot.

| Game event | Audio |
| --- | --- |
| Start | `mission_impossible_theme.mp3`, loops while running |
| Laser trip | `alarm.wav`, starts immediately and stops after the alarm siren phase |
| Finish | `win_theme.mp3`, plays once |
| Stop/reset | Playback stops and the PCM buffer is cleared |

Audio controls available through serial:

```text
list
status
connect AA:BB:CC:DD:EE:FF
disconnect
auto on
auto off
play <file>
```

`data/` is the LittleFS payload. The current media budget is intentionally
close to the 2 MB filesystem limit:

| File | Size | Playback |
| --- | --- | --- |
| `mission_impossible_theme.mp3` | 1,896,742 bytes | 118.5 s, loops while running |
| `alarm.wav` | 73,908 bytes | Short loop during an alarm |
| `win_theme.mp3` | 109,976 bytes | One-shot victory theme |

Only filenames of 32 characters or fewer are supported by the LittleFS build.

## Serial reset capture

Reset the board and capture boot output without an interactive monitor:

```sh
python scripts/serial_reset.py --port /dev/ttyUSB0
```

If boot output reports no I2C device, check LCD power, ground, address, cable
continuity, and signal levels before relying on the display.
