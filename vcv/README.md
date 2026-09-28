# Slow LFO for VCV Rack 2

This 8HP virtual module previews the proposed 8×8 RGB user experience. It generates ideal phase-aligned sine, triangle, and square outputs in either 0–5 V or −5–5 V. Its panel SVG is generated from the `Slow-LFO-8HP-Faceplate_8x8.kicad_pcb` front silk and copper artwork. VCV models the controls and timing; the hardware firmware and circuit are developed separately.

## Use

- Click the encoder to advance Minutes → Hours → Days → Months. Four 4×4 color quadrants fill the matrix; the bright quadrant is selected.
- Drag the encoder vertically or scroll over it to change period. The ranges are 1–60 minutes, 1–24 hours, 1–64 days in half-day steps, and 1–64 fixed 30-day months. Minutes and Hours fill a colored pie. Days and Months fill the 8×8 grid from the upper left, one pixel per day or month; the next day pixel is half-bright at half-day steps. A full violet grid represents a 64-month cycle.
- Hold the encoder for 0.5 seconds to preview the current voltage mode: four blue upper rows for 0–5 V, or blue upper and red lower rows for −5–5 V. Keep holding to 2 seconds to switch all outputs immediately; the preview changes with them. Further holding does nothing. Release between 0.5 and 2 seconds to leave the mode unchanged. A tap shorter than 0.5 seconds still advances the edit unit.
- After two seconds, the matrix returns to a static triangle glyph in the selected unit's color. The glyph stays visibly colored while one white, extra-bright pixel follows the shared phase. In 0–5 V mode the glyph and marker stay in the upper four rows; in −5–5 V mode they span the full matrix. All three CV jacks remain active at all times.
- On module creation and after switching simulated power back on, a 64-pixel spiral shows reserve-capacitor charging. The default is 15 real seconds, with 10- and 25-second comparison settings in the context menu. Phase and encoder edits pause while charging; CV outputs hold the saved phase. Simulated power off blanks the matrix and drives the output jacks to 0 V. Time acceleration does not accelerate charging.
- The module context menu also exposes 1×/60×/3600×/86400× time acceleration, forced phase, and simulated power off/on. These extra controls are VCV-only.
- Open the context menu to see the exact period. Connect the three output jacks to Rack's Scope to inspect their voltages.

The physical hardware's present sine/triangle RC filters support a provisional **10-second minimum target** for usable shape. The revised 1-minute minimum has ample calculated margin, pending bench measurement. A fixed month means 30 days, not a calendar month; 64 months is 1,920 days. The selected 0.33 F capacitor and 10 Ω charge resistor give an ideal 3.3-second RC constant; the actual reserve-ready delay is estimated at roughly **10–25 seconds**. The 15-second VCV animation is a UX approximation, not a measured capacitor model.

## Build and install

Install a Rack 2 plugin SDK and pass its `Rack-SDK` directory as `RACK_DIR`. The SDK is a build dependency, not project source. `make` regenerates `res/SlowLFO.svg` from the included 8×8 KiCad faceplate when that board changes; this requires Python 3 and `kicad-cli`. Set `KICAD_CLI` to its executable if it is not on `PATH`.

```sh
cd vcv
make -j4 RACK_DIR=/absolute/path/to/Rack-SDK
make dist RACK_DIR=/absolute/path/to/Rack-SDK
```

`dist/` contains a `.vcvplugin` package. Rack loads installed plugins at startup; restart Rack after installation. The module appears as **Slow LFO Prototype → Slow LFO**.
