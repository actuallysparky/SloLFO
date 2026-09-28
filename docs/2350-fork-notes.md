# RP2350-Matrix 8×8 carrier fork

`Slow-LFO-8HP-Carrier-RevB_2350.kicad_pcb` is a copy of the saved
`_pinheaders` carrier with U1 alone replaced by a centered, front-mounted
Waveshare RP2350-Matrix 8×8 module footprint. The module's USB-C connector is
at the top, behind its LED side. All existing tracks, vias, zones, other
footprints, and board edges are unchanged. The new U1 footprint uses the 20
through-hole edge connections; the eight small bottom-edge GPIO solder pads
and DOUT are intentionally absent, since this is the header-mount variant.

| Carrier net | RP2350-Matrix pin | U1 pad |
| --- | --- | ---: |
| `VSYS` | `5V` | 1 |
| `GND` | `GND` | 2 |
| `3V3` | `3V3` | 3 |
| `CAP_SENSE` | `GP27` / ADC1 | 6 |
| `BUCK_SENSE` | `GP26` / ADC0 | 7 |
| `PWM_SINE` | `GP10` | 11 |
| `PWM_TRI` | `GP11` | 12 |
| `PWM_SQUARE` | `GP12` | 13 |
| `ENC_A` | `GP13` | 14 |
| `ENC_B` | `GP14` | 15 |
| `ENC_SW` | `GP15` | 16 |

Other edge pins are left unconnected. `GP25` controls the onboard WS2812
matrix and needs no carrier net. Firmware for this fork must use GPIO 10–12
for waveform PWM, GPIO 13–15 for the encoder, GPIO 26–27 for power sensing,
and GPIO 25 for the LEDs. The existing RP2040 firmware/pin map will not work
unchanged. `VSYS` is the carrier's existing net name; the new module pin is
marked `5V` by Waveshare.

The outline and 2.54 mm edge-hole pitch follow Waveshare's [25 × 25 mm
mechanical drawing](https://www.waveshare.com/img/devkit/RP2350-Matrix/RP2350-Matrix-details-size.jpg);
the pin names/order follow its [pinout image](https://www.waveshare.com/img/devkit/RP2350-Matrix/RP2350-Matrix-details-inter.jpg).
The Ø1.0 mm drill and Ø1.7 mm annulus are inherited header assumptions, not
dimensions specified by Waveshare. Dry-fit a purchased module and chosen
headers before fabrication. The separate `Slow-LFO-8HP-Faceplate_8x8.kicad_pcb`
fork enlarges the LED aperture from 12 × 11 mm to 22 × 22 mm, centered over
the 25 × 25 mm U1 module at faceplate drawing coordinate (67.37, 46.5475) mm.
This leaves a nominal 1.5 mm module rim on all four sides. Per the owner's
direction, **only the four Edge.Cuts segments defining the LED aperture changed**.
All artwork, labels, rack holes, encoder, and output jack holes remain at their
original positions for the owner to move. The current artwork crosses the new
aperture: KiCad DRC reports 58 violations (26 copper-edge, 28 silk-edge, and
the six inherited original-faceplate findings). The actual LED envelope,
header stack, and panel-to-module spacing still need a physical dry-fit. Rear USB clearance,
LED current budget, nearby carrier component fit, and all new U1 copper
escapes are still to be resolved. Neither fork is a fabrication release.
