# Wiring Guide

## Pin Assignments

| Function | GPIO | Pico Pin | Notes |
|----------|------|----------|-------|
| I2S DATA | GP26 | Pin 31 | Serial audio data to DAC |
| I2S BCK | GP27 | Pin 32 | Bit clock |
| I2S LRCK | GP28 | Pin 34 | Word select (auto: BCK+1) |
| MIDI RX | GP5 | Pin 7 | UART1 RX |

## PCM5102A DAC Wiring

```
Pico                    PCM5102A
─────                   ────────
GP26 (DATA) ──────────► DIN
GP27 (BCK)  ──────────► BCK
GP28 (LRCK) ──────────► LCK
3V3         ──────────► VCC
GND         ──────────► GND
                        SCK ──► GND (uses internal clock)
                        FMT ──► GND (I2S format)
                        XMT ──► 3V3 (unmute)
```

## MIDI Input Circuit

Standard serial MIDI input uses an H11L1 optocoupler for electrical isolation. The H11L1 has a built-in Schmitt trigger output, which provides clean digital edges without external components.

```
MIDI DIN 5-pin                   H11L1                      Pico
─────────────                    ─────                      ────
                           ┌───────────────┐
Pin 4 (+5V) ──► 220Ω ────►─┤ 1 (Anode)     │
                           │               │
Pin 5 (data) ─────────────►┤ 2 (Cathode)   │
                           │               │
                       ┌───┤ 3 (GND)       │
                       │   │               │
                       │   │     6 (VCC) ──┼──► 3V3
                       │   │               │
                       │   │     5 (VO) ───┼──► GP5 (UART1 RX)
                       │   │               │
                       │   │     4 (NC)    │
                       │   └───────────────┘
                       │
                       └──► GND

Pin 2 (GND) ──► shield ground (do NOT connect to Pico GND directly)
```

**H11L1 Pin Connections:**
- **Pin 1 (Anode)**: MIDI pin 4 via 220Ω resistor
- **Pin 2 (Cathode)**: MIDI pin 5 (directly connected)
- **Pin 3 (GND)**: Connect to Pico GND
- **Pin 4 (NC)**: Not connected
- **Pin 5 (VO)**: Output to GP5 (no pull-up needed, Schmitt trigger output)
- **Pin 6 (VCC)**: Connect to 3V3

## Power

- The Pico can be powered via USB or VSYS
- The PCM5102A module typically has its own voltage regulator and can be powered from 3V3 or 5V depending on the breakout board
