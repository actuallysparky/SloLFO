#!/usr/bin/env python3
"""Fail the build if an RP2350 image reaches the reserved 64 KiB journal."""
from pathlib import Path
import sys
binary = Path(sys.argv[1])
limit = 16 * 1024 * 1024 - 64 * 1024
size = binary.stat().st_size
print(f'Firmware image {size} bytes; journal begins at {limit} bytes')
if size > limit:
    raise SystemExit('Firmware overlaps reserved journal')
