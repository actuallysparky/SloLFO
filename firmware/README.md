# Rev B RP2040-Matrix firmware

This is the current hardware firmware source, targeting the earlier Waveshare RP2040-Matrix with a 5×5 LED display. It has only been built as an ELF/raw BIN, not flashed or run on hardware. The RP2350/8×8 carrier and VCV interaction need a firmware port; their GPIO map and display behavior differ from this source.

## Build

Set `PICO_SDK_PATH` to a local Pico SDK checkout, then run:

```sh
cmake -S firmware -B /tmp/slow-lfo-revb-build -DPICO_BOARD=pico -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/slow-lfo-revb-build -j4
```

The post-build check rejects an image extending into the last 64 KiB of the nominal 2 MiB flash. The optional `-DSLOW_LFO_UF2=ON` needs a working `picotool` installation. Do not flash a raw binary before confirming the module's installed flash capacity, its boot ROM flashing flow, and board revision.

GPIO2/3/4 drive sine/triangle/square PWM, GPIO5/6/7 read encoder A/B/push, GPIO26 senses the buck, GPIO27 senses the supercapacitor, and GPIO29 drives the onboard WS2812 matrix. GPIO26/27 dividers are nominally 1:2. The ADC thresholds (4.4 V fail, 4.75 V recover, 4.55 V reserve-ready) are prototype defaults and require scoped rail calibration.

The same 64-bit timer phase feeds all outputs at 100 Hz. The 25 LED frame alternates qualitative waveform previews and a log progress display. A short press changes edit mode; a long press changes all outputs between 0–5 V and −5–5 V without changing phase. At cold boot the stored phase and CV are held until the supercapacitor reaches the ready threshold. The phase freezes during an outage; no RTC is present.

The final 64 KiB of flash is a 256-page journal. Hourly and power-fail records include period, phase, voltage mode, sequence, and CRC. A partly programmed page is skipped on the next write; a whole sector is erased only while stable power is present. A real power-cut trial must confirm that the module can blank the matrix, program a page, and remain above its VSYS operating limit with aged, low-tolerance capacitance. The firmware currently offers no service interface for calibration or telemetry, so production commissioning needs an added diagnostic path.
