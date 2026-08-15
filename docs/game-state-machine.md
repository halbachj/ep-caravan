# Game State Machine

`src/main.cpp` owns the game state machine. The timer class supplies elapsed
time, but game state determines whether the laser inputs, LEDs, LCD, and audio
are active.

## States

| State | Timer | Lasers | LCD | LEDs | Audio |
| --- | --- | --- | --- | --- | --- |
| `READY` | Zeroed or stopped | Enabled | `Elapsed Time:` | Off | Off |
| `RUNNING` | Counting | Monitored | Live elapsed time | Static dim red | Main theme loops |
| `ALARM_BLINKING` | Frozen | Three output blinks, then off | `LASER n TRIPPED` and final time | Previous frame | Alarm starts immediately |
| `ALARM_SIREN` | Frozen | Off | Trip result | Six rotating red beams for 5 s | Alarm continues |
| `ALARM_FLASHING` | Frozen | Off | Trip result | Three full-strip red flashes | Off |
| `ALARM_LATCHED` | Frozen | Off | Trip result | Solid red | Off |
| `VICTORY_BLINKING` | Frozen | Off immediately | `WON!` and final time blinks three times | Previous frame | Win theme plays once |
| `VICTORY_SIREN` | Frozen | Off | `WON!` and final time | Six rotating green beams for 5 s | No repeating alarm audio |
| `VICTORY_FLASHING` | Frozen | Off | `WON!` and final time | Three full-strip green flashes | Off |
| `VICTORY_LATCHED` | Frozen | Off | `WON!` and final time | Solid green | Off |

The red and green sirens advance one LED position every 15 ms but render every
30 ms. This keeps their apparent speed while reducing WS2812 transmission time
and protecting Bluetooth audio playback.

## Transition diagram

```text
boot
  |
  | laser-switch HIGH/LOW cycle x3, then enabled
  v
READY <-----------------------------------------------+
  |                                                     |
  | GPIO4 press or serial start                         | GPIO4 press or serial reset
  v                                                     | (blocked while GPIO5 is LOW)
RUNNING                                                  |
  |  |  |                                               |
  |  |  +-- serial stop --------------------------------+
  |  |
  |  +----- GPIO5 finish switch LOW --> VICTORY_BLINKING
  |
  +-------- any laser input LOW ------> ALARM_BLINKING

ALARM_BLINKING --> ALARM_SIREN --> ALARM_FLASHING --> ALARM_LATCHED
      1.5 s           5 s             1.5 s

VICTORY_BLINKING --> VICTORY_SIREN --> VICTORY_FLASHING --> VICTORY_LATCHED
        1.5 s             5 s              1.5 s
```

## Inputs and guards

- Laser inputs are examined only in `RUNNING`. The first LOW input wins; later
  laser changes cannot alter the recorded trip number.
- The finish switch is examined only in `RUNNING`. A finish event always wins
  over later laser input because the state leaves `RUNNING` immediately.
- GPIO4 has two roles: it starts from `READY`; otherwise it resets to `READY`.
- Reset is rejected while GPIO5 is LOW. This applies to GPIO4 and the serial
  `reset` command. Boot uses a forced reset to establish the initial state.
- Serial `stop` only acts from `RUNNING` and returns directly to `READY`.

## Timing constants

| Constant | Value | Meaning |
| --- | --- | --- |
| `LASER_BOOT_TOGGLES` | 3 | Boot laser output cycles |
| `LASER_BOOT_TOGGLE_MS` | 250 ms | Each HIGH or LOW boot phase |
| `LASER_BLINKS` | 3 | Loss laser-output blink count |
| `LASER_BLINK_MS` | 250 ms | Loss laser-output phase duration |
| `SIREN_DURATION_MS` | 5,000 ms | Alarm and victory siren duration |
| `LED_FLASHES` | 3 | Final red or green flash count |
| `LED_FLASH_MS` | 250 ms | Flash phase duration |
| `BUTTON_DEBOUNCE_MS` | 50 ms | Action and finish input debounce |

## Reset effects

Reset clears the timer and recorded laser number, enables laser power, clears
