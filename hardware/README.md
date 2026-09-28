# Hardware snapshot

`Slow-LFO-8HP-Carrier-RevB_2350.kicad_pcb` and `Slow-LFO-8HP-Faceplate_8x8.kicad_pcb` are copies of the saved RP2350/8×8 forks. `SlowLFO.pretty` contains only the custom footprints referenced by this carrier, and `fp-lib-table` points KiCad to that directory.

These files are **not fabrication-ready**. The source project's saved-carrier review found DRC violations and an unconnected `-12V` power-header pair; the faceplate has copper and silkscreen overlapping the 22 × 22 mm matrix opening. Header fit, component clearances, LED current, reserve hold-up, and analog outputs need physical validation. See [fork notes](../docs/2350-fork-notes.md) and the [functional specification](../docs/functional-spec.md).

Open either PCB in KiCad for inspection. Before fabrication, rerun DRC on these exact files after any changes, independently review the electrical topology, and dry-fit the purchased module, encoder, jacks, and rack hardware.
