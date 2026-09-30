# SloLFO

SloLFO is an 8 HP slow Eurorack LFO module and a VCV Rack 2 plugin. The VCV plugin is availalbe prior to the eurorack module and is being used as an exploring ground for the user interface. Three phase-aligned outputs provide sine, triangle, and square CV. The module uses one push encoder and an 8×8 color matrix modeled on the Waveshare RP2350-Matrix hardware fork.

**Author for the VCV plugin: Bearamin.** Inspired by Patience, with thanks to the wonders of slow time.

## Try the VCV module

| Action | Result |
| --- | --- |
| Tap the encoder for under 0.5 seconds | Select Minutes, Hours, Days, or Months; the selected 4×4 color quadrant is bright. |
| Turn the encoder | Set 1–60 minutes, 1–24 hours, 1–64 days in half-day steps, or 1–64 fixed 30-day months. |
| Hold for 0.5 seconds | Preview the present voltage mode: four blue upper rows for 0–5 V, or blue upper and red lower rows for −5–5 V. |
| Continue holding to 2 seconds | Switch all three outputs immediately and update the preview; a longer hold does not switch again. |

Minutes and Hours show period as a colored pie. Days and Months fill the grid from the upper left, one pixel per day or month; half-day settings half-light the next pixel. At idle, a colored triangle remains static while one bright pixel follows phase. Its upper-half shape indicates unipolar output; bipolar uses the display's full height. The three CV outputs remain active together.

The plugin is a user-experience prototype. A fixed month is 30 days, so the longest setting is 1,920 days. The included firmware now targets the RP2350/8×8 module and follows the same front-panel controls and displays. Physical behavior remains to be checked on an assembled module.

## Contents

- [`vcv/`](vcv/) — Rack plugin C++ source, panel SVG, manifest, build files, and panel generator.
- [`firmware/`](firmware/) — RP2350/8×8 firmware source, local board definition, and build instructions.
- [`hardware/`](hardware/) — RP2350 carrier and 8×8 faceplate KiCad PCB files plus the custom footprints they use.
- [`bom/`](bom/) — a current board-derived parts list and an older priced Rev B workbook, with revision notes.
- [`docs/functional-spec.md`](docs/functional-spec.md) — proposed behavior and electrical architecture.
- [`docs/2350-fork-notes.md`](docs/2350-fork-notes.md) — RP2350 pin mapping and open hardware issues.

## Build the Rack plugin

Install the VCV Rack 2 plugin SDK and set `RACK_DIR` to its extracted `Rack-SDK` directory. The checked-in panel SVG lets a normal plugin build use the existing artwork; regenerating it from the faceplate requires KiCad's `kicad-cli` and Python 3.

```sh
cd vcv
make RACK_DIR=/absolute/path/to/Rack-SDK
make dist RACK_DIR=/absolute/path/to/Rack-SDK
```

The `.vcvplugin` package appears under `vcv/dist/`. The plugin manifest uses the `SlowLFOPrototype` slug and `Bearamin` author.

## License and project status

SloLFO is licensed under [GPL-3.0-or-later](LICENSE), the license identifier used by [VCV Core](https://github.com/VCVRack/Rack/blob/v2/Core.json). TUpdates to the plugin, firmware, and boards will be published here as the design evolves. See the [firmware build and pin map](firmware/README.md) and [hardware files](hardware/README.md).
