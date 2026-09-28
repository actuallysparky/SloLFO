# RP2350-Matrix firmware

This Pico SDK firmware targets the Waveshare RP2350-Matrix with its 8×8 RGB display and the SloLFO carrier. It implements the VCV module's four literal period units, encoder gestures, static triangle phase view, colored selector, pie/calendar period views, and simultaneous sine, triangle, and square outputs. The hardware charging spiral follows measured reserve voltage; VCV uses a timed simulation.

## Build

Use Pico SDK 2.3.1 or newer and an Arm embedded GCC toolchain. From the repository root, set `PICO_SDK_PATH` to a local SDK checkout and run:

```sh
cmake -S firmware -B /tmp/slolfo-rp2350-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_NO_PICOTOOL=1
cmake --build /tmp/slolfo-rp2350-build -j4
```

The build creates `slow_lfo.elf` and `slow_lfo.bin` in the build directory. With a suitable `picotool`, omit `-DPICO_NO_PICOTOOL=1` and set `-DSLOW_LFO_UF2=ON` to request UF2 output. The local board header selects RP2350A and the module's nominal 16 MB flash. The post-build check keeps the binary out of the final 64 KiB flash journal.

## Controls and outputs

- A tap shorter than 0.5 seconds advances Minutes → Hours → Days → Months and shows four 4×4 color quadrants, with the selected quadrant bright.
- Turning sets 1–60 minutes in 1-minute steps, 1–24 hours in 1-hour steps, 1–64 days in half-day steps, or 1–64 fixed 30-day months in 1-month steps. The first detent in a new unit clamps an out-of-range period to that unit's nearest endpoint. The period display returns to the waveform after two seconds.
- From 0.5 seconds of a press, the matrix previews the current output mode: blue upper rows for 0–5 V; blue upper and red lower rows for −5–5 V. At two seconds the mode switches once, immediately. Turning while pressed cancels the hold action.
- Minutes and Hours fill a color pie. Days and Months fill from the upper-left in calendar order. The idle triangle has a bright moving phase pixel and occupies only the upper four rows in unipolar mode.

GP10/11/12 generate sine/triangle/square PWM; GP13/14/15 read encoder A/B/push; GP26 senses the buck rail; GP27 senses the reserve capacitor; GP25 drives the WS2812 chain. The code assumes alternating physical LED rows. The board's ADC divider ratios are provisionally 1:2; 4.4 V fail, 4.75 V recover, and 4.55 V reserve-ready are prototype thresholds. Confirm the actual LED orientation, rail thresholds, and PWM-to-jack voltages on the assembled board before using a binary.

One shared 64-bit timer phase drives all three outputs at 100 Hz. Charging freezes the phase and encoder edits until the reserve threshold is met. The phase also freezes while unpowered; no backed-up RTC is present. The final 64 KiB of flash holds 256-byte CRC journal records with period, phase, voltage mode, and selected unit. It checkpoints hourly and appends an emergency record after buck loss, without erasing during reserve power. A red lower-right pixel indicates a failed flash operation or an invalid record found at boot. The 16 MB flash capacity, one-second hold-up margin, and interrupted-write recovery still require physical confirmation.
