# EP Caravan

ESP32 development project based on PlatformIO, with a reproducible Nix
devshell and direnv integration.

## Prerequisites

- [Nix](https://nixos.org/) with flakes enabled
  (`experimental-features = nix-command flakes`)
- [direnv](https://direnv.net/) (optional but recommended)
- An ESP32 board connected over USB (this project targets the generic
  ESP32 DevKit V1, detected as `/dev/ttyUSB0` via a CP2102 UART bridge)

## Project structure

```
.
├── flake.nix          # Nix flake: reproducible devshell (PlatformIO + Python)
├── .envrc             # direnv: auto-enter the devshell via `use flake`
├── platformio.ini     # PlatformIO build/upload/monitor configuration
├── src/
│   ├── main.cpp       # Firmware entry point: LCD, serial, buttons, display logic
│   ├── timer.h        # Timer state machine (header)
│   └── timer.cpp      # Timer state machine (IDLE/RUNNING/STOPPED + elapsed time)
├── scripts/
│   └── serial_reset.py# Reset the board and capture boot serial output
└── .gitignore
```

## Development environment

The `flake.nix` provides a devshell containing:

- **PlatformIO** (`pio`) — the build system / CLI
- **Python 3 with pyserial** — powers `scripts/serial_reset.py`
- **git**

### Option A: direnv (recommended)

Enter the directory; direnv prompts to allow the environment once:

```sh
direnv allow
```

From then on the devshell loads automatically whenever you `cd` into the
project. Reload after editing `flake.nix` (or run `direnv reload`).

### Option B: manual shell

```sh
nix develop
```

Both options put `pio` and the Python environment on `PATH`.

## Workflow

### Build

```sh
pio run
```

Compiles the firmware; the binary lands in `.pio/build/esp32dev/`.

### Upload (flash)

```sh
pio run -t upload
```

Writes the firmware to the board via `esptool` over the serial port.

### Monitor serial

```sh
pio device monitor -b 115200
```

Opens an interactive serial console. **Requires a real terminal (TTY)** —
it fails in a non-interactive shell.

### Reset + capture boot log

The firmware prints `Hello, World!` once during `setup()`. To trigger a
fresh boot and see that output without opening a terminal:

```sh
python scripts/serial_reset.py
```

This pulses the board's `EN` pin (via the serial `RTS` line) to reboot it,
then captures everything printed on the serial port for a few seconds.

## LCD display (JoyIT SBC-LCD16x2)

The project drives a Joy-IT **SBC-LCD16x2** 16x2 character LCD over I2C.
It is an HD44780-compatible display with a soldered **PCF8574AT** I2C
backpack, so any standard `LiquidCrystal_I2C` library works (no vendor
library is required — JoyIT's own manual recommends the same one).

## WS2812B LED strip (running pattern)

A 300-LED WS2812B strip runs a comet pattern on **GPIO25**: a bright head
with a fading tail travels from the first LED to the last, wrapping
around. Each LED keeps a fixed hue (rainbow along the strip) so the
moving head is easy to track.

> **Do not use GPIO39.** The ESP32's GPIO 34–39 are input-only pins and
> cannot drive WS2812B data. Use an output-capable pin like GPIO25.

### Wiring

| LED strip | ESP32 DevKit V1 |
| --------- | --------------- |
| `DATA` (DIN) | `GPIO25` |
| `5V` | `VIN` (5V rail) |
| `GND` | `GND` |

> **Power.** At full white a 300-LED WS2812B strip draws up to ~18 A.
> The firmware caps brightness at 64/255 (≈4.5 A), but for a real
> (non-test) load power the strip from a dedicated 5 V supply with a
> common GND — do not rely on the USB port's 5 V rail.

### Wiring

| LCD | ESP32 DevKit V1 |
| --- | --------------- |
| `VCC` | `VIN` (5V rail) |
| `GND` | `GND` |
| `SDA` | `GPIO21` |
| `SCL` | `GPIO22` |

> **Power at 5V, not 3.3V.** The module is a 5V device; powered from
> `3V3` the text is barely visible. Move `VCC` to `VIN` (the 5V USB
> rail). The ESP32's I2C still works because I2C is open-drain: the ESP32
> only pulls the lines low, and the module's pull-ups (tied to its 5V
> `VCC`) provide the high level. JoyIT rates the module for exactly this
> (3.3V logic at 5V supply). Caveat: the ESP32's SDA/SCL pins then see
> ~5V (above the 3.6V absolute max); current is pull-up-limited and this
> is standard practice, but an I2C level shifter is the strict option.

### I2C address

The backpack address depends on its `A0/A1/A2` jumpers. Common values
are `0x27` and `0x3F`. `src/main.cpp` **auto-detects** the address at
boot by scanning the bus and logs the result to serial.

### Contrast

The module has a contrast trimpot on the back. If text is invisible or
faint, adjust it with a small screwdriver while the display is on. This
is analog — it cannot be changed from software.

### Firmware

```cpp
lcd = new LiquidCrystal_I2C(findLcdAddress(), 16, 2);
lcd->init();
lcd->backlight();
lcd->setCursor(0, 0);
lcd->print("Hello, World!");
```

## Timer (feature/lcd-timer)

The firmware implements a stopwatch timer shown on the LCD.

### LCD states

| State | Row 1 | Row 2 | Behavior |
| ----- | ----- | ----- | -------- |
| Idle / reset | `Elapsed Time:` | `mm:ss:000` | static, zeroed |
| Running | `Elapsed Time:` | `mm:ss:000` | value refreshes every 50 ms |
| Stopped | `Final Time:` | `mm:ss:000` | screen cleared, blinks 3x at 0.5 s, then solid |

Time format is `mm:ss:mmm` (minutes : seconds : milliseconds, the `µµµ`
of the requirement). `mm` can exceed 99 (3-digit minutes) so 30+ minute
runs display fine. Sub-second resolution is limited to 50 ms by the
refresh rate.

### Serial control (buttons not wired yet)

Send one line-terminated command at 115200 baud:

| Command | Effect |
| ------- | ------ |
| `start` | start or resume the timer |
| `stop`  | freeze the timer and blink `Final Time:` |
| `reset` | zero the timer back to the idle state |

### Buttons (dummy, inactive)

`src/main.cpp` contains a debounced `handleButtons()` for two active-low
buttons on `BTN_START_PIN` (GPIO4) and `BTN_STOP_PIN` (GPIO2). It is
defined but **not called** from `loop()` and the pins are not configured
yet. To enable: call `pinMode(pin, INPUT_PULLUP)` for both pins in
`setup()` and add `handleButtons();` to `loop()`.

## Board configuration

`platformio.ini` pins the environment to the generic ESP32 DevKit:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_port = /dev/ttyUSB0
monitor_port = /dev/ttyUSB0
```

## Notes

- PlatformIO writes its toolchain and packages to `~/.platformio`
  (shared across projects).
- The `pio` binary installed via Nix is also available globally
  (`nix profile install nixpkgs#platformio-core`), but the devshell keeps
  the toolchain pinned and reproducible.
