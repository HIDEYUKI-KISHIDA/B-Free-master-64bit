#!/usr/bin/env python3
"""Wayland core (wayland.xml) vs B-Free compositor: accurate coverage CSV.

Sources of truth for opcodes: child order in Program/bfree_x86_64/wayland-upstream.xml
Classification: gui_server/wayland_server_runtime.c + gui_server_main.c (wire path).

Run: py -3 tools/wayland_core_bfree_coverage.py
Output: ../wayland_core_bfree_coverage.csv
"""
from __future__ import annotations

import csv
import os
import sys
import xml.etree.ElementTree as ET

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BFREE_X86 = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
XML_PATH = os.path.join(BFREE_X86, "wayland-upstream.xml")
OUT_CSV = os.path.join(BFREE_X86, "wayland_core_bfree_coverage.csv")


def parse_protocol(xml_path: str) -> tuple[list[tuple[str, int, str]], list[tuple[str, int, str]]]:
    root = ET.parse(xml_path).getroot()
    requests: list[tuple[str, int, str]] = []
    events: list[tuple[str, int, str]] = []
    for iface in root.findall("interface"):
        iname = iface.get("name") or ""
        for i, req in enumerate(iface.findall("request")):
            requests.append((iname, i, req.get("name") or ""))
        for i, ev in enumerate(iface.findall("event")):
            events.append((iname, i, ev.get("name") or ""))
    return requests, events


# --- Overrides: (interface, opcode) -> (status, notes) ----------------------------

REQ: dict[tuple[str, int], tuple[str, str]] = {
    ("wl_display", 0): ("implemented", "sync: gui_server_main minimal (primary) + dispatch"),
    ("wl_display", 1): ("implemented", "get_registry: minimal advertises globals; dispatch duplicates"),
    ("wl_registry", 0): ("implemented", "bind: wl_registry_bind in dispatch_wayland_request (main no longer short-circuits)"),
    ("wl_compositor", 0): ("implemented", "create_surface"),
    ("wl_compositor", 1): ("implemented", "create_region"),
    ("wl_shm_pool", 0): ("implemented", "create_buffer"),
    ("wl_shm_pool", 1): ("implemented", "destroy pool"),
    ("wl_shm_pool", 2): ("implemented", "resize"),
    ("wl_shm", 0): ("implemented", "create_pool (+ optional SCM_RIGHTS fd)"),
    ("wl_shm", 1): ("implemented", "release (since 2)"),
    ("wl_buffer", 0): ("implemented", "destroy"),
    ("wl_data_offer", 0): ("implemented", "accept"),
    ("wl_data_offer", 1): ("implemented", "receive"),
    ("wl_data_offer", 2): ("implemented", "destroy"),
    ("wl_data_offer", 3): ("implemented", "finish"),
    ("wl_data_offer", 4): ("implemented", "set_actions"),
    ("wl_data_source", 0): ("implemented", "offer"),
    ("wl_data_source", 1): ("implemented", "destroy"),
    ("wl_data_source", 2): ("implemented", "set_actions (since 3)"),
    ("wl_data_device", 0): ("implemented", "start_drag: enter/motion/drop/leave + source dnd events"),
    ("wl_data_device", 1): ("implemented", "set_selection + broadcast selection"),
    ("wl_data_device", 2): ("implemented", "release"),
    ("wl_data_device_manager", 0): ("implemented", "create_data_source"),
    ("wl_data_device_manager", 1): ("implemented", "get_data_device"),
    ("wl_shell", 0): ("implemented", "get_shell_surface"),
    ("wl_shell_surface", 0): ("implemented", "pong"),
    ("wl_shell_surface", 1): ("implemented", "move"),
    ("wl_shell_surface", 2): ("implemented", "resize"),
    ("wl_shell_surface", 3): ("implemented", "set_toplevel"),
    ("wl_shell_surface", 4): ("implemented", "set_transient"),
    ("wl_shell_surface", 5): ("implemented", "set_fullscreen"),
    ("wl_shell_surface", 6): ("implemented", "set_popup"),
    ("wl_shell_surface", 7): ("implemented", "set_maximized"),
    ("wl_shell_surface", 8): ("implemented", "set_title"),
    ("wl_shell_surface", 9): ("implemented", "set_class"),
    ("wl_surface", 0): ("implemented", "destroy"),
    ("wl_surface", 1): ("implemented", "attach"),
    ("wl_surface", 2): ("implemented", "damage (accepted no-op)"),
    ("wl_surface", 3): ("implemented", "frame + callback.done on commit"),
    ("wl_surface", 4): ("implemented", "set_opaque_region (accepted no-op)"),
    ("wl_surface", 5): ("implemented", "set_input_region (accepted no-op)"),
    ("wl_surface", 6): ("implemented", "commit (+ fbdev/gpu blit)"),
    ("wl_surface", 7): ("implemented", "set_buffer_transform"),
    ("wl_surface", 8): ("implemented", "set_buffer_scale"),
    ("wl_surface", 9): ("implemented", "damage_buffer (accepted no-op)"),
    ("wl_surface", 10): ("implemented", "offset (accepted no-op)"),
    ("wl_seat", 0): ("implemented", "get_pointer"),
    ("wl_seat", 1): ("implemented", "get_keyboard"),
    ("wl_seat", 2): ("implemented", "get_touch (object only; touch requests stub)"),
    ("wl_seat", 3): ("implemented", "release"),
    ("wl_pointer", 0): ("implemented", "set_cursor accepted"),
    ("wl_pointer", 1): ("implemented", "release"),
    ("wl_keyboard", 0): ("implemented", "release"),
    ("wl_touch", 0): ("implemented", "release"),
    ("wl_output", 0): ("implemented", "release (+ reassign surfaces)"),
    ("wl_output", 1): ("partial", "non-standard opcode 1 (refresh vendor)"),
    ("wl_output", 2): ("partial", "non-standard opcode 2 set_mode"),
    ("wl_output", 3): ("partial", "non-standard opcode 3 set_scale"),
    ("wl_region", 0): ("implemented", "destroy"),
    ("wl_region", 1): ("implemented", "add"),
    ("wl_region", 2): ("implemented", "subtract"),
    ("wl_subcompositor", 0): ("implemented", "destroy"),
    ("wl_subcompositor", 1): ("implemented", "get_subsurface"),
    ("wl_subsurface", 0): ("implemented", "destroy"),
    ("wl_subsurface", 1): ("implemented", "set_position"),
    ("wl_subsurface", 2): ("implemented", "place_above (accepted)"),
    ("wl_subsurface", 3): ("implemented", "place_below (accepted)"),
    ("wl_subsurface", 4): ("implemented", "set_sync"),
    ("wl_subsurface", 5): ("implemented", "set_desync"),
}

# Vendor opcodes on wl_data_source (not in wayland.xml) — tracked only in notes row
EXTRA_NOTE = (
    "2026-05-09: runtime path is wayland_server_runtime.c; wl_surface/frame-damage-* accepted; "
    "input forwarding (pointer/keyboard), v5 pointer axis_source/stop/frame, seat name, "
    "subcompositor/subsurface minimal, data-device selection path and popup minimal added."
)

EV: dict[tuple[str, int], tuple[str, str]] = {
    ("wl_display", 0): ("implemented", "error: send_wl_display_error"),
    ("wl_display", 1): ("implemented", "delete_id on object removal paths"),
    ("wl_registry", 0): ("implemented", "global: minimal + wl_registry_bind"),
    ("wl_registry", 1): ("implemented", "global_remove on wl_output release"),
    ("wl_callback", 0): ("implemented", "done: send_wl_callback_done"),
    ("wl_shm", 0): ("implemented", "format: sent on wl_shm bind"),
    ("wl_buffer", 0): ("implemented", "release on surface.commit"),
    ("wl_data_offer", 0): ("implemented", "offer (mime): send_wl_data_offer_mime"),
    ("wl_data_offer", 1): ("implemented", "source_actions: send_wl_data_offer_source_actions in dnd/offer setup"),
    ("wl_data_offer", 2): ("implemented", "action: send on set_actions"),
    ("wl_data_source", 0): ("implemented", "target"),
    ("wl_data_source", 1): ("implemented", "send"),
    ("wl_data_source", 2): ("implemented", "cancelled"),
    ("wl_data_source", 3): ("implemented", "dnd_drop_performed"),
    ("wl_data_source", 4): ("implemented", "dnd_finished"),
    ("wl_data_source", 5): ("implemented", "action"),
    ("wl_data_device", 0): ("implemented", "data_offer"),
    ("wl_data_device", 1): ("implemented", "enter"),
    ("wl_data_device", 2): ("implemented", "leave"),
    ("wl_data_device", 3): ("implemented", "motion"),
    ("wl_data_device", 4): ("implemented", "drop"),
    ("wl_data_device", 5): ("implemented", "selection"),
    ("wl_shell_surface", 0): ("implemented", "ping"),
    ("wl_shell_surface", 1): ("implemented", "configure"),
    ("wl_shell_surface", 2): ("implemented", "popup_done"),
    ("wl_surface", 0): ("implemented", "enter"),
    ("wl_surface", 1): ("implemented", "leave"),
    ("wl_surface", 2): ("implemented", "preferred_buffer_scale"),
    ("wl_surface", 3): ("implemented", "preferred_buffer_transform"),
    ("wl_seat", 0): ("implemented", "capabilities: send_wl_seat_capabilities on bind"),
    ("wl_seat", 1): ("implemented", "name"),
    ("wl_pointer", 0): ("implemented", "enter"),
    ("wl_pointer", 1): ("implemented", "leave"),
    ("wl_pointer", 2): ("implemented", "motion"),
    ("wl_pointer", 3): ("implemented", "button"),
    ("wl_pointer", 4): ("implemented", "axis"),
    ("wl_pointer", 5): ("implemented", "frame (v5+)"),
    ("wl_pointer", 6): ("implemented", "axis_source (v5+)"),
    ("wl_pointer", 7): ("implemented", "axis_stop (v5+)"),
    ("wl_pointer", 8): ("implemented", "axis_discrete (since 5)"),
    ("wl_pointer", 9): ("implemented", "axis_value120 (since 8)"),
    ("wl_pointer", 10): ("implemented", "axis_relative_direction (since 9)"),
    ("wl_keyboard", 0): ("implemented", "keymap (incl. NO_KEYMAP path)"),
    ("wl_keyboard", 1): ("implemented", "enter"),
    ("wl_keyboard", 2): ("implemented", "leave"),
    ("wl_keyboard", 3): ("implemented", "key: input forwarding"),
    ("wl_keyboard", 4): ("implemented", "modifiers"),
    ("wl_keyboard", 5): ("implemented", "repeat_info (v4+)"),
    ("wl_touch", 0): ("implemented", "down"),
    ("wl_touch", 1): ("implemented", "up"),
    ("wl_touch", 2): ("implemented", "motion"),
    ("wl_touch", 3): ("implemented", "frame"),
    ("wl_touch", 4): ("implemented", "cancel"),
    ("wl_touch", 5): ("implemented", "shape (since 6)"),
    ("wl_touch", 6): ("implemented", "orientation (since 6)"),
    ("wl_output", 0): ("implemented", "geometry"),
    ("wl_output", 1): ("implemented", "mode"),
    ("wl_output", 2): ("implemented", "done"),
    ("wl_output", 3): ("implemented", "scale"),
    ("wl_output", 4): ("implemented", "name (since 4)"),
    ("wl_output", 5): ("implemented", "description (since 4)"),
}


def apply_defaults(
    rows: list[tuple[str, int, str]],
    d: dict[tuple[str, int], tuple[str, str]],
    kind: str,
) -> None:
    for iface, opc, name in rows:
        k = (iface, opc)
        if k not in d:
            d[k] = ("missing", f"not yet audited — default missing ({kind} {iface}.{name})")


def count_status(rows: list[tuple[str, int, str]], d: dict) -> dict[str, int]:
    out: dict[str, int] = {}
    for iface, opc, _ in rows:
        st = d[(iface, opc)][0]
        out[st] = out.get(st, 0) + 1
    return out


def main() -> int:
    global XML_PATH, OUT_CSV
    if len(sys.argv) > 1:
        XML_PATH = sys.argv[1]
    requests, events = parse_protocol(XML_PATH)
    apply_defaults(requests, REQ, "request")
    apply_defaults(events, EV, "event")

    with open(OUT_CSV, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["kind", "identifier", "interface", "opcode", "member", "status", "notes"])
        w.writerow(["meta", "", "", "", "", "", EXTRA_NOTE])
        for iface, opc, name in requests:
            st, note = REQ[(iface, opc)]
            w.writerow(["request", f"{iface}_{name}", iface, opc, name, st, note])
        for iface, opc, name in events:
            st, note = EV[(iface, opc)]
            w.writerow(["event", f"{iface}_{name}", iface, opc, name, st, note])

    print(f"Wrote {OUT_CSV}")
    print(f"requests n={len(requests)} by_status={count_status(requests, REQ)}")
    print(f"events n={len(events)} by_status={count_status(events, EV)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
