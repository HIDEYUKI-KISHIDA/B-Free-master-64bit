#!/usr/bin/env python3
"""Create a simple LED + resistor circuit using KiCad MCP server tools."""

import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "kicad-mcp-server" / "src"))

from kicad_mcp_server.tools.project import create_kicad_project
from kicad_mcp_server.tools.schematic_editor import (
    add_component_from_library,
    add_label,
    add_wire,
)

PROJECT_DIR = Path(__file__).resolve().parent / "led_blinker"
PROJECT_NAME = "led_blinker"
SCH = str(PROJECT_DIR / f"{PROJECT_NAME}.kicad_sch")


async def main() -> None:
    print(await create_kicad_project(str(PROJECT_DIR), PROJECT_NAME, title="Simple LED Circuit"))

    print(await add_component_from_library(
        SCH, "Device", "R", "R1", "330", footprint="Resistor_SMD:R_0805_2012Metric",
        x=120, y=100,
    ))
    print(await add_component_from_library(
        SCH, "Device", "LED", "D1", "Red", footprint="LED_SMD:LED_0805_2012Metric",
        x=150, y=100,
    ))
    print(await add_component_from_library(
        SCH, "power", "VCC", "#PWR01", "VCC", x=100, y=90,
    ))
    print(await add_component_from_library(
        SCH, "power", "GND", "#PWR02", "GND", x=150, y=110,
    ))

    print(await add_wire(SCH, [(105, 90), (115, 90), (115, 100)]))
    print(await add_wire(SCH, [(125, 100), (145, 100)]))
    print(await add_wire(SCH, [(155, 100), (155, 105), (150, 105)]))
    print(await add_label(SCH, "VCC", 105, 90))
    print(await add_label(SCH, "GND", 150, 105))


if __name__ == "__main__":
    asyncio.run(main())
