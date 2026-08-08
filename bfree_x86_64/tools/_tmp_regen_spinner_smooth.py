#!/usr/bin/env python3
"""
Regenerate splash frames:
  - Sheet order: RIGHT column top→bottom, then LEFT column top→bottom
  - Smooth spin: 12 frames @ 30° from a single base cell (trail rotates correctly)
  - Emit kernel brand data + guest pixel arrays (keep guest arm footer)
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = Path(
    "/mnt/c/Users/h_kis/.cursor/projects/"
    "c-Users-h-kis-Desktop-B-Free-master-Program-bfree-x86-64/assets"
)
# Prefer newer sheet if present, else original
SPIN_CANDIDATES = [
    ASSETS / "c__Users_h_kis_AppData_Roaming_Cursor_User_workspaceStorage_empty-window_images_____-fc4dc946-1437-4525-b49e-19f8f320d459.png",
    ASSETS / "c__Users_h_kis_AppData_Roaming_Cursor_User_workspaceStorage_empty-window_images_____-132b43ad-94fd-4fd3-b5c0-652ca617e5b0.png",
]
LOGO = ASSETS / "c__Users_h_kis_AppData_Roaming_Cursor_User_workspaceStorage_empty-window_images_ROGO_BFREE_TRON-74bcb7f1-9dd8-4537-9594-31f71fc7252e.png"
SPLASH = ROOT / "userland/desktop_qt/splash"
GUEST_CPP = ROOT / "userland/desktop_qt/guest_splash_data.cpp"
GUEST_HDR = ROOT / "userland/desktop_qt/guest_splash_data.h"
KERN_C = ROOT / "kernel/fb_splash_brand_data.c"
KERN_H = ROOT / "kernel/fb_splash_brand_data.h"
FOOTER = ROOT / "tools/_tmp_guest_splash_runtime_footer.cpp"

SIZE = 56
N_SMOOTH = 12


def white_to_alpha(im: Image.Image, thr=245) -> Image.Image:
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if r >= thr and g >= thr and b >= thr:
                px[x, y] = (255, 255, 255, 0)
    return im


def tight(im: Image.Image) -> Image.Image:
    bbox = im.getbbox()
    return im.crop(bbox) if bbox else im


def to_argb(im: Image.Image):
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    out = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            out.append((a << 24) | (r << 16) | (g << 8) | b)
    return w, h, out


def emit_array(name, w, h, pixels, f, static=True):
    st = "static " if static else ""
    f.write(f"{st}const unsigned {name}_w = {w}u;\n")
    f.write(f"{st}const unsigned {name}_h = {h}u;\n")
    f.write(f"{st}const uint32_t {name}_px[{w * h}] = {{\n")
    for i, v in enumerate(pixels):
        if i % 8 == 0:
            f.write("  ")
        f.write(f"0x{v:08X},")
        f.write("\n" if i % 8 == 7 else " ")
    if len(pixels) % 8:
        f.write("\n")
    f.write("};\n\n")


def pick_spin():
    for p in SPIN_CANDIDATES:
        if p.is_file():
            return p
    raise SystemExit("spinner sheet not found")


def sheet_cells_right_then_left(path: Path):
    im = Image.open(path).convert("RGBA")
    W, H = im.size
    cols, rows = 2, 5
    cw, rh = W // cols, H // rows
    cells = []
    # Right column (c=1) top→bottom, then left (c=0) top→bottom
    for c in (1, 0):
        for r in range(rows):
            cell = im.crop((c * cw, r * rh, (c + 1) * cw, (r + 1) * rh))
            cell = tight(white_to_alpha(cell))
            cell = cell.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
            cell = tight(white_to_alpha(cell, thr=250))
            # Square canvas for rotation
            canvas = Image.new("RGBA", (SIZE, SIZE), (255, 255, 255, 0))
            ox = (SIZE - cell.width) // 2
            oy = (SIZE - cell.height) // 2
            canvas.paste(cell, (ox, oy), cell)
            cells.append(canvas)
            print(f"sheet cell c={c} r={r} -> idx {len(cells)-1}")
    return cells


def smooth_frames(base: Image.Image):
    frames = []
    for i in range(N_SMOOTH):
        # Clockwise lead: negative rotate in PIL
        fr = base.rotate(-i * (360 / N_SMOOTH), resample=Image.Resampling.BICUBIC, expand=False)
        fr = white_to_alpha(fr, thr=250)
        frames.append(fr)
    return frames


def main():
    SPLASH.mkdir(parents=True, exist_ok=True)
    spin_path = pick_spin()
    print("sheet", spin_path)
    sheet = sheet_cells_right_then_left(spin_path)
    # Save ordered sheet extracts for inspection
    for i, cell in enumerate(sheet):
        cell.save(SPLASH / f"sheet_order_{i:02d}.png")
    # Smooth animation from first cell of user order (right-top)
    frames = smooth_frames(sheet[0])
    for i, fr in enumerate(frames):
        fr.save(SPLASH / f"spinner_{i:02d}.png")
        print(f"spinner_{i:02d} smooth")

    logo = tight(white_to_alpha(Image.open(LOGO)))
    lw = 280
    lh = max(1, int(logo.height * lw / logo.width))
    logo = logo.resize((lw, lh), Image.Resampling.LANCZOS)
    logo = tight(white_to_alpha(logo, thr=250))
    logo.save(SPLASH / "bfree_tron_logo.png")

    # --- kernel ---
    with KERN_H.open("w", encoding="utf-8", newline="\n") as f:
        f.write("#pragma once\n#include <stdint.h>\n")
        f.write("extern const unsigned g_brand_logo_w;\n")
        f.write("extern const unsigned g_brand_logo_h;\n")
        f.write("extern const uint32_t g_brand_logo_px[];\n")
        f.write("extern const unsigned g_brand_nframes;\n")
        f.write("extern const uint32_t *const g_brand_sp_px[];\n")
        f.write("extern const unsigned g_brand_sp_w[];\n")
        f.write("extern const unsigned g_brand_sp_h[];\n")

    with KERN_C.open("w", encoding="utf-8", newline="\n") as f:
        f.write("/* Auto-generated by tools/_tmp_regen_spinner_smooth.py */\n")
        f.write('#include "fb_splash_brand_data.h"\n\n')
        w, h, px = to_argb(logo)
        emit_array("g_brand_logo", w, h, px, f, static=False)
        f.write(f"const unsigned g_brand_nframes = {len(frames)}u;\n")
        for i, fr in enumerate(frames):
            w, h, px = to_argb(fr)
            emit_array(f"g_brand_sp{i:02d}", w, h, px, f, static=False)
        f.write("const uint32_t *const g_brand_sp_px[] = {\n")
        for i in range(len(frames)):
            f.write(f"  g_brand_sp{i:02d}_px,\n")
        f.write("};\n")
        f.write("const unsigned g_brand_sp_w[] = {\n  ")
        f.write(", ".join(f"g_brand_sp{i:02d}_w" for i in range(len(frames))))
        f.write("\n};\n")
        f.write("const unsigned g_brand_sp_h[] = {\n  ")
        f.write(", ".join(f"g_brand_sp{i:02d}_h" for i in range(len(frames))))
        f.write("\n};\n")

    # --- guest pixels + footer ---
    with GUEST_HDR.open("w", encoding="utf-8", newline="\n") as f:
        f.write("#pragma once\n#include <stdint.h>\n")
        f.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n")
        f.write("int guest_splash_arm(void);\n")
        f.write("void guest_splash_show(unsigned frame);\n")
        f.write("void guest_splash_advance(void);\n")
        f.write("int guest_splash_ready(void);\n")
        f.write("#ifdef __cplusplus\n}\n#endif\n")

    footer = FOOTER.read_text(encoding="utf-8") if FOOTER.is_file() else ""
    with GUEST_CPP.open("w", encoding="utf-8", newline="\n") as f:
        f.write("/* Auto-generated by tools/_tmp_regen_spinner_smooth.py — pixels + footer. */\n")
        f.write('#include "guest_splash_data.h"\n')
        f.write('#include "../../gui_server/integration_gui/bfree_qpa/bfree/bfree_guest_abi.h"\n')
        f.write("#include <stdint.h>\n#include <cstddef>\n#include <cstdint>\n\n")
        w, h, px = to_argb(logo)
        emit_array("g_splash_logo", w, h, px, f, static=True)
        f.write(f"static const unsigned g_splash_nframes = {len(frames)}u;\n")
        for i, fr in enumerate(frames):
            w, h, px = to_argb(fr)
            emit_array(f"g_splash_sp{i:02d}", w, h, px, f, static=True)
        f.write("static const uint32_t *const g_splash_sp_px[] = {\n")
        for i in range(len(frames)):
            f.write(f"  g_splash_sp{i:02d}_px,\n")
        f.write("};\n")
        f.write("static const unsigned g_splash_sp_w[] = {\n  ")
        f.write(", ".join(f"g_splash_sp{i:02d}_w" for i in range(len(frames))))
        f.write("\n};\n")
        f.write("static const unsigned g_splash_sp_h[] = {\n  ")
        f.write(", ".join(f"g_splash_sp{i:02d}_h" for i in range(len(frames))))
        f.write("\n};\n\n")
        f.write(footer)

    print("wrote", KERN_C, GUEST_CPP)


if __name__ == "__main__":
    main()
