#!/usr/bin/env python3
"""Regenerate the VCV panel from the saved 8x8 KiCad faceplate fork."""

from __future__ import annotations

import copy
import hashlib
import os
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
PCB = PROJECT / "hardware/Slow-LFO-8HP-Faceplate_8x8.kicad_pcb"
OUTPUT = Path(__file__).resolve().parent / "res/SlowLFO.svg"
KICAD_CLI = os.environ.get("KICAD_CLI") or shutil.which("kicad-cli") or \
    "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
SVG = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG)

PANEL_WIDTH_MM = 40.30
PANEL_HEIGHT_MM = 128.50
PCB_X_ORIGIN_MM = 0.17


def tag(name: str) -> str:
    return f"{{{SVG}}}{name}"


def recolor(node: ET.Element, before: str, after: str) -> None:
    for element in node.iter():
        for key, value in list(element.attrib.items()):
            element.set(key, value.replace(before, after))


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="slow-lfo-vcv-panel-") as directory:
        temp = Path(directory)
        subprocess.run(
            [
                str(KICAD_CLI), "pcb", "export", "svg", "--mode-multi",
                "--fit-page-to-board", "--exclude-drawing-sheet",
                "--layers", "F.SilkS,F.Cu", "--output", str(temp), str(PCB),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        stem = PCB.stem
        silk = ET.parse(temp / f"{stem}-F_Silkscreen.svg").getroot()
        copper = ET.parse(temp / f"{stem}-F_Cu.svg").getroot()

        export_w = float(silk.get("width", "").removesuffix("mm"))
        export_h = float(silk.get("height", "").removesuffix("mm"))
        margin_x = (export_w - PANEL_WIDTH_MM) / 2
        margin_y = (export_h - PANEL_HEIGHT_MM) / 2
        root = ET.Element(
            tag("svg"),
            {
                "width": "120", "height": "380",
                "viewBox": f"{margin_x:.4f} {margin_y:.4f} {PANEL_WIDTH_MM} {PANEL_HEIGHT_MM}",
            },
        )
        ET.SubElement(root, tag("title")).text = "Slow LFO 8x8 faceplate fork preview"
        digest = hashlib.sha256(PCB.read_bytes()).hexdigest()
        ET.SubElement(root, tag("desc")).text = f"KiCad faceplate SHA-256 {digest}"
        ET.SubElement(
            root, tag("rect"),
            {
                "x": f"{margin_x:.4f}", "y": f"{margin_y:.4f}",
                "width": str(PANEL_WIDTH_MM), "height": str(PANEL_HEIGHT_MM),
                "fill": "#07090c",
            },
        )

        for exported, source_color, target_color in (
            (copper, "#C83434", "#cd9b46"),
            (silk, "#F2EDA1", "#f4f2e8"),
        ):
            for child in exported:
                if child.tag != tag("g"):
                    continue
                drawing = copy.deepcopy(child)
                recolor(drawing, source_color, target_color)
                root.append(drawing)

        def x_at(board_x: float) -> float:
            return board_x - PCB_X_ORIGIN_MM + margin_x

        def y_at(board_y: float) -> float:
            return board_y + margin_y

        # Mechanical apertures from layout-notes.md. Black covers art behind
        # cutouts, and the Rack LED/encoder/port widgets overlay the openings.
        for x, y, width, height in (
            (16.82, 7.77, 7.0, 3.8),    # USB opening
            (9.32, 11.57, 22.0, 22.0),  # 8x8 LED opening, panel-local coordinates
        ):
            ET.SubElement(root, tag("rect"), {
                "x": f"{x_at(x):.4f}", "y": f"{y_at(y):.4f}",
                "width": str(width), "height": str(height),
                "fill": "#07090c", "stroke": "#536169", "stroke-width": "0.10",
            })
        for x, y, radius in (
            (20.32, 48.77, 3.55),
            (20.32, 76.7871, 3.20),
            (20.32, 93.0245, 3.20),
            (20.32, 110.0692, 3.20),
            (7.62, 3.00, 1.60),
            (33.02, 3.00, 1.60),
            (7.62, 125.50, 1.60),
            (33.02, 125.50, 1.60),
        ):
            ET.SubElement(root, tag("circle"), {
                "cx": f"{x_at(x):.4f}", "cy": f"{y_at(y):.4f}",
                "r": f"{radius:.4f}", "fill": "#07090c",
                "stroke": "#536169", "stroke-width": "0.10",
            })

        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        ET.ElementTree(root).write(OUTPUT, encoding="utf-8", xml_declaration=True)
        svg = OUTPUT.read_text(encoding="utf-8")
        OUTPUT.write_text("\n".join(line.rstrip(" \t") for line in svg.splitlines()) + "\n", encoding="utf-8")
        print(f"Wrote {OUTPUT} from {PCB.name} ({digest[:12]})")


if __name__ == "__main__":
    main()
