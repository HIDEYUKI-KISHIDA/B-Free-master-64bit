# AGENTS.md

## Cursor Cloud specific instructions

This repo is a TRON/BTRON-family operating-system **monorepo** with several
independent product lines. The actively developed, primary buildable+runnable
target is **`bfree_x86_64`** (a T-Kernel 2.0 x86_64 micro-kernel + POSIX/musl
userland). Legacy lines (`btron-pc`, `PC9801`) are Windows/MSYS2-oriented; the
root `README.md` quickstart (`btron-pc/boot/2nd` `make 2ndboot64`) does **not**
work on Linux (no `2ndboot64` rule; needs an ELF32/MinGW toolchain). The ARM
trees at the repo root (`kernel_arm/`, `userland_arm/`) are source-only (no
Makefile) and `smartphone-tron-os/` is spec/docs only.

### 本デスク / Native OS progress
Canonical checklist: `bfree_x86_64/docs/HONDESK_TODOLIST.ja.md`.
Four tracks (do not mix): **Native OS** = whole guest OS (boots; daily
bfree-QPA desk is N2, not Wayland, not GPU). **Wayland** = compositor
socket + client windows (W7 C done, **W8 done**). **本デスク** =
product desk (`DesktopShell.qml` as Qt Wayland client; **D1 done**,
**D2 first hop done** — stub hello carries
`compositor_stub/DesktopShell.qml`, paints GuestMvpShell bits,
`getenv=wayland`, `exit_group`, `[wl] vfork parent`. **no**
`QQmlEngine` / `beginCreate`. Product QML IR waits until asked.
D3 not started). **GPU** = accel (G* not started). Stub history:
`docs/HONDESK_PHASES.ja.md`. Scripts live under `bfree_x86_64/`
(`cd` there, not `$HOME`).
W8 **done** (gold/navy/cyan + `[wl] vfork parent`).
**D1 done:** stub hello `getenv QT_QPA_PLATFORM=wayland` then
`exit_group` then `[wl] vfork parent`. Daily `bfree.iso` was
**not** overwritten (N2 EX/TE stays). Do not overwrite
`desktop_qt/guest_link_compat.o`. **D2 first hop done:** serial
`D2 qml-client` / `hello wait-stub` / `getenv=wayland` /
`D2 argv -platform wayland` / `D2 fill desk` / `exit_group` /
`[wl] vfork parent`. Window is wallpaper `#7A8FA8` + white card +
EX/VW/TE tiles (not W8 gold). Surrounding EX/VW/TE is stub 仮 chrome.
Keep 480×320 SHM. **D2b started:** optional `QQmlEngine` on the Wayland
hello (`[qt] D2b qml-engine` / `D2b engine enter` / `D2b engine ok`).
No `QQmlComponent`, no `loadUrl`, no `qml_register_types`, no
`beginCreate`. If D2b link fails, keep D2 bits-only hello. If it hangs
after `D2b engine enter`, rebuild with `BFREE_D2B_QML=0`. Do not link
`libQt6Qml.a` into `desktop.elf` from this path. Do not
`beginCreate`. Do not `execve("desktop.elf")` on g1-desk. Do not
compositor `fork`(57). Do not map a from-source kernel onto the stub
ISO (S2).

### Toolchain (installed by the startup update script)
- A prebuilt **`x86_64-elf` cross GCC 13.2.0** is installed to
  `$HOME/x86_64-elf-toolchain` by `bfree_x86_64/tools/install_x86_64_elf_gpp_prebuilt.sh`.
  `bfree_x86_64/kernel/Makefile` **auto-detects** it from
  `$HOME/x86_64-elf-toolchain/bin` (or `/root/x86_64-elf-toolchain/bin`), so you
  do **not** need to edit `PATH` for the kernel build. Override with
  `make BFREE_ELF_GCC_ROOT=...` if needed.
- `musl-gcc` (musl-tools), `qemu-system-x86_64`, `grub-mkrescue`+`xorriso`+
  `mtools`, `nasm`, `python3`, `make`, `file` come from apt and are on `PATH`.

### What builds/runs cleanly from a fresh checkout
- **musl guest userland:** `make -C bfree_x86_64/userland/musl_hello` →
  `hello.elf` (static x86_64 musl ELF). Running it prints `B2_MUSL_HELLO_OK`.
- All kernel translation units **cross-compile** with the toolchain above.

### Known gaps (code/packaging, NOT environment issues)
The committed state of this branch is **not** a clean, fully buildable OS; the
maintainer builds from a local, partially-uncommitted working tree. Expect:
- **Never-committed Qt-desk inputs** (gitignored or simply absent):
  `bfree_x86_64/userland/embedded/`,
  `bfree_x86_64/iso_root/` (runtime copy of `iso_skel/`),
  `bfree_x86_64/userland/init/`, `bfree_x86_64/userland/busybox_guest/`, and
  `bfree_x86_64/build.sh`. Hello ISO does **not** need those:
  `tools/make_hello_iso.sh` uses committed `iso_skel/` + `make -C kernel`.
  The documented full Qt-desk flows
  (`tools/build_busybox_iso.sh`, the `tools/*_smoke.sh` scripts) do **not** run
  as-is from a clean checkout.
The committed kernel **does** `make -C bfree_x86_64/kernel` from a
clean checkout: skip missing `../multiboot2_header.S` (header is in
`reset.S`), keep committed `user_hello_elf.c` if `userland/embedded/`
is absent, define `BFREE_MAX_SIGNALFD` / `BFREE_SIGNALFD_FD_BASE`, and
define `g_bfree_shell_text_fp*` so `--no-undefined` links. Hello ISO:
`bash bfree_x86_64/tools/make_hello_iso.sh` → `bfree-hello.iso` (never
`bfree.iso`). Desk ISO: `bash bfree_x86_64/tools/make_desk_iso.sh` →
`bfree-desk.iso` (no kernel rebuild, no desktop relink). If daily
`bfree.iso` is present it is copied under a new name; if absent the
script downloads GitHub Release tag `desk-goldens-1`
(`kernel.elf.g1-desk`, Qt `desktop.elf` ≥ 10MB, `init.elf`,
`busybox.elf`) and assembles. Never commit `bfree.iso` (123MB).
Recipe: `bfree_x86_64/docs/ISO_RECIPE.ja.md`. Hello ISO is
**not** the Qt desk. Daily `bfree.iso` / `kernel.elf.g1-desk` stay
untouched. From-source kernel is still not the daily g1-desk binary.
Do not `git checkout` this branch onto the maintainer working tree;
apply with `git show origin/<branch>:path > path`.
Qt desk persist: attach `-drive file=persist.img,if=ide,index=0,media=disk,format=raw`
(create with `bash tools/_f1_persist_img_scaffold.sh`). EX writes `/persist/desk.txt`.
Without `-drive`, `/persist` does not survive reboot.

Curated tests `userland/ltp_curated` and `userland/libc_test_curated` fail to
  build against Ubuntu's musl 1.2.4 (`renameat2`, `struct statx`) — version
  sensitive.

### Booting the OS in QEMU (once the kernel builds)
The kernel is a **Multiboot2** ELF; boot progress goes to the **serial port**
and a "B-Free TRON" splash is drawn to the framebuffer:
```
grub-mkrescue -o /tmp/bfree.iso <iso_root>   # iso_root/boot/kernel.elf + boot/grub/grub.cfg (multiboot2 /boot/kernel.elf)
qemu-system-x86_64 -m 512M -no-reboot -cdrom /tmp/bfree.iso -display none -serial file:/tmp/serial.log
```
On boot it runs built-in self-tests (STAGE1 T-Kernel dual-task, STAGE2 timer,
STAGE3 ioport/snapshot) then loads userland (`init.elf`→`shell.elf`→embedded
`user_hello.elf`). Capture a framebuffer screenshot via the QEMU monitor
`screendump` command. With no `init.elf`/`shell.elf`/`busybox` modules present,
it falls back to the committed embedded `user_hello.elf`.

### Guest DesktopShell / Wayland gate 1
Product `qrc:/DesktopShell.qml` URL construction (`PreferSynchronous` or
Asynchronous-on-boot) hangs the guest QML type-loader **before** qmlcache
lookup. `QQmlComponent::setData` historically page-faults (`@0x29000000`).
Boot must skip that load (`skip DesktopShell.qml boot load`) and keep the
FB chrome path. Gate 1 (QML IR Ready) runs only after
`QML ready, entering event loop`: empty `QQmlComponent(engine)` then
`loadUrl(qrc:/GuestGate1Window.qml, Asynchronous)`. Do not wait on boot.
Do not load product `DesktopShell.qml` on this path. Success serial:
`G1 product QML Ready`. Timeout: `G1 thin QML timeout (still Loading)`
means `loadUrl(Asynchronous)` posted but IR stayed `status=2` because the
desk loop skips `processEvents`. A bounded G1-only pump
(`ExcludeUserInputEvents`, 16 spins) runs right after `loadUrl`; if it
hangs, last line is `G1 pump spin=N` without `G1 pump ok`. Observed:
16× `G1 pump ok` then `status=2` timeout — `processEvents` is safe after
the loop but does **not** run the type-loader QThread. Guest pthreads are
cooperative (`libstdc++ threads=no`); G1 pump must also call
`bfree_guest_qt_coop_schedule()`. Observed after that: 64× `G1 pump ok`
then `status=2` timeout and **no** `qmlcache lookup`. The type-loader
QThread never runs; further `loadUrl` / `processEvents` / coop pumps
will not reach Ready. Next Gate 1 work must instantiate
`guest_gate1_window_qmlcache` on the main thread (no QQmlTypeLoader).
`guest_mvp_qmlcache_lookup` already maps `qrc:/GuestGate1Window.qml` to
`guest_gate1_window_cached_unit` and would print `qmlcache HIT`.
`QQmlTypeData::initializeFromCachedUnit` asserts the type-loader
thread, so `loadUrl` cannot complete on this guest. Next: call lookup
from the main thread (no `loadUrl`), then either instantiate the
`CachedQmlUnit` without TypeData or run the type-loader inline on main.
Observed: `qmlcache lookup` + `qmlcache HIT GuestGate1Window` +
`G1 cache unit ok` after a direct main-thread lookup. `loadUrl` is not
required for HIT. `beginCreate` still needs `initializeFromCachedUnit`
on the type-loader thread (`assertTypeLoaderThread`). Next: make
`QQmlTypeLoaderThread::isThisThread()` true on the main thread (or run
the loader inline) so PreferSynchronous/`getType` can finish. Guest
already has `tools/qqmlthread_guest_single.cpp` and
`tools/rebuild_guest_qtdeclarative_singlethread.sh`. Confirmed on the WSL guest Qt: `ftw/qqmlthread.cpp` is the stock
threaded loader and `libQt6Qml.a` exports
`QQmlThreadPrivate::threadEvent`. The single-thread stub was not
applied. Force `QQmlThread::isThisThread()` to return true, rebuild
`Qml`, copy `build-qtdeclarative/lib/libQt6Qml.a` into the prefix
`lib/`, relink `desktop.elf`, then after the proven main-thread
`qmlcache HIT` call `loadUrl(GuestGate1Window, PreferSynchronous)`
only — never on boot. If serial stops at `G1 sync loadUrl begin`,
the inline loader still hangs. A global `isThisThread() { return true; }` plus a full Qml
reconfigure PFd at STAGE 5 `qrc gui_shaders` /
`qresource ensure list d=0` (`CR2=0x8`, RIP user read) — before
`skip DesktopShell` / G1. Revert `isThisThread` to the stock body
and restore G1 to main-thread lookup only (no `PreferSynchronous`
`loadUrl`). Relinking `guest_main.o` alone is not enough: the
crashing `libQt6Qml.a` (always-true `isThisThread`) must be
rebuilt after reverting `qqmlthread.cpp`, then copied to the
prefix and the desktop ELF relinked. Same RIP `0x3766BB9` at
`qrc gui_shaders` means the old Qml archive is still linked.
`cmake --build --target Qml` can print `ninja: no work to do`
after a source revert if `qqmlthread.cpp.o` is still newer than
the `.cpp` (WSL/mtime). That leaves the 11:06 always-true
archive in place. Force the object: `rm` the `.o`, `touch` the
`.cpp`, then rebuild. The build log must compile
`qml/ftw/qqmlthread.cpp.o` (not "no work"). Prefix
`lib/libQt6Qml.a` mtime must be newer than that always-true
archive. Observed good rebuild: `[5/5] Linking CXX static
library lib/libQt6Qml.a` and prefix mtime `13:22`. Stock
`isThisThread` in the unlinked `.o` is `jmp` + reloc to
`isCurrentThread` (`objdump -d -r -C`); plain `-d` will not
print that name. `strings desktop.elf` showing
`G1 cache lookup enter` only proves `guest_main.o`. Relink
`desktop` after the new archive; do not ISO the pre-rebuild
75322720 ELF.
Observed after the 13:22 stock Qml + 13:26 relink: still PF at
`qrc gui_shaders` / `CR2=0x8`, but RIP moved
`0x3766BB9` → `0x3766989`. That is not the old always-true
archive. Rebuilding `qqmlthread.cpp.o` shifts guest .text/BSS.
`holder=0x62c41a0` in serial is the compile-time
`BFREE_GUEST_QT_RESOURCE_HOLDER_VA`; `qresource registry n=0`
is that address, not Qt's real list. After any Qml/desktop
relink, run the VA loop (`tools/update_guest_resource_holder_va.sh`
then rebuild `guest_main.o` + `guest_link_compat.o` + `desktop`)
until `nm` holder equals the header. Do not rebuild Qml again
to "fix" this PF. Do not reintroduce a boot URL ctor. Use
`bfree_x86_64/tools/force_rebuild_stock_qqmlthread.sh` on WSL.
Observed after VA converge to `0x62c4160` (stock Qml 13:22,
desktop relink): `holder=0x62c4160`, `qrc gui_shaders` with
no `CR2=8`, `skip DesktopShell.qml boot load`,
`DesktopShell.qml Ready (native Gate1 + FB chrome)`,
`entering event loop`, `G1 cache lookup enter`,
`qmlcache HIT GuestGate1Window`, `G1 cache unit ok`, then
live FB input (`desk open Terminal`, `wm close hit`).
That is FB restore, not QML IR Ready / not Wayland.
Known-good backup (do not overwrite):
`$HOME/out/bfree-good-20260816/{libQt6Qml.a,desktop.elf,guest_resource_holder_va.h}`.
Restore with `bfree_x86_64/tools/restore_bfree_good_20260816.sh`.
Next Gate 1 experiment: runtime flag `bfree_guest_typeloader_main_ok`
set only after the event loop, then `PreferSynchronous` for
`GuestGate1Window` after the proven HIT. Never always-true from
boot. Apply `tools/patch_qqmlthread_runtime_flag.sh` then
`tools/force_rebuild_stock_qqmlthread.sh`, then VA-loop relink.
Success serial: `typeloader main ok=1` then `G1 sync loadUrl end`
then `G1 thin QML Ready`. Observed after the post-loop flag +
`PreferSynchronous`: 64× `G1 pump ok`, `status=2`, then
`G1 thin QML timeout (still Loading)`. No `CR2=8`. The runtime
flag does not finish the type-loader; more `loadUrl` / pump is
the same dead path. Next Gate 1 must instantiate the cached
unit without `QQmlTypeLoader` / `loadUrl`. Restore the
2026-08-16 backup for the working FB desk.
Cache-instantiate experiment (no `loadUrl`, no `beginCreate`):
`tools/patch_g1_cache_instantiate.py`. Qt 6.8
`ExecutableCompilationUnit::create` takes
`QQmlRefPointer<CompiledData::CompilationUnit>&&` plus
`QV4::ExecutionEngine*` (`QQmlEnginePrivate::v4engine()`),
not `unique_ptr` plus `QQmlEngine*`. Observed: that `create()`
call hangs the guest. Do not retry `create()`, `loadUrl`,
pumps, or boot `isThisThread` always-true. Restore
`desktop.elf.good-running` (75322720) or
`$HOME/out/bfree-good-20260816`. Next probe only:
`tools/patch_g1_cache_skip_create.py` reads `qmlData` words
and returns. Success serial: `G1 cache instantiate skip create`
then `G1 qmlData w0=`. Observed on the 75326984 ELF
(`holder=0x62c5160`): `instantiate enter`, `data ok`,
`skip create`, `qmlData=0x3bda680`,
`w0=0x63347671 w1=0x61746164 w2=0x42 w3=0x00060800`
(`qv4cdata`, Qt 6.8.0). Desk still lives. That is a readable
cache unit, not IR Ready and not Wayland. Do not retry
`create()`. Do not rebuild Qml. Keep
`desktop.elf.good-running` (75322720, holder `0x62c4160`)
and `desktop.elf.skip-create` (75326984, holder `0x62c5160`).
Keep `desktop.elf.cu-only` (75326984, holder `0x62c5160`) as the
proven CompilationUnit construct+assign ELF. Do not overwrite
any of the three. Next Gate 1 probe:
`tools/patch_g1_cache_cu_only.py` constructs
`QV4::CompiledData::CompilationUnit` and assigns `qmlData`.
It does not call `ExecutableCompilationUnit::create()`.
Success serial: `G1 cache cu begin` then `G1 cache cu ok`.
Observed: `cu begin`, `cu ok`, `cu data=0x3bda6e0` (same as
`qmlData`). `new CompilationUnit` and assigning `data` work.
The hang is specifically `ExecutableCompilationUnit::create()`.
On hang/PF restore `desktop.elf.skip-create`. Keep a
`desktop.elf.cu-only` copy of this ELF. Do not retry `create()`.
Next: `tools/patch_g1_cache_attach_no_create.py`.
`ExecutableCompilationUnit::create` is private in Qt 6.8.
Use public `ExecutionEngine::executableCompilationUnit(cu)`.
Observed attach path: `v4engine()->executableCompilationUnit(cu)` then
`priv->compilationUnit = exec`. Serial (`holder=0x62c6160`,
75331080 ELF): `exec engine ok`, `attach ok`,
`G1 IR status=1`, `G1 thin QML Ready`. That is Gate 1 thin
QML Ready for `GuestGate1Window` cache unit. It is not
product `DesktopShell.qml`, not `beginCreate`, not Wayland.
Keep `desktop.elf.g1-ready` (75331080, holder `0x62c6160`) as the
proven Gate 1 thin QML Ready ELF. Do not overwrite it.
`ExecutableCompilationUnit::create()`. Do not unskip Wayland
until a later gate. `tools/patch_g1_begincreate.py` called
`beginCreate` only (no `completeCreate`). Observed: `G1 thin QML Ready`
then `G1 beginCreate begin` then `CR2=0xC`. That was
**without** `populate()`. Do not retry `beginCreate` on the
Ready-only ELF. Qt 6.8 `executableCompilationUnit()` does not
call `populate()` (`runtimeStrings` stays null). Observed after
`tools/patch_g1_populate.py` (holder `0x62c6160`, 75331080 ELF):
`G1 thin QML Ready`, `G1 populate begin`, `G1 populate ok`,
`G1 runtimeStrings=0x436b188` (non-zero). No `CR2`. Keep
`desktop.elf.g1-populate` as this ELF. Do not overwrite
`g1-ready` or `g1-populate`. This is still not
`beginCreate`, not product `DesktopShell.qml`, not Wayland.
Next: `tools/patch_g1_begincreate_after_populate.py` calls
`beginCreate` only after `G1 populate ok` and non-null
`runtimeStrings`. No `completeCreate`. Observed: `G1 populate ok`,
`G1 runtimeStrings=0x436b188`, then `G1 beginCreate begin`, then
`CR2=0xC` — same fault as Ready-only. `populate()` does **not**
fix `beginCreate`. Do not retry `beginCreate` / `completeCreate`.
Restore `desktop.elf.g1-populate`. Ready+populate stands. Not Wayland.
Do not retry `beginCreate`. Qt 6.8 `beginCreate` uses
`priv->start` (default **-1**) and `state.creator()->create(start)`.
The empty-component attach never sets `start` / `url` / `typeData`.
Observed dump after populate (`holder=0x62c6160`): `G1 start=0xffffffff`
(-1), `G1 typeData null`, `G1 url empty`, `G1 ctx=0x424dc68` (live).
No `CR2`. After `priv->start = 0` and
`priv->url = qrc:/GuestGate1Window.qml`: `G1 start=0`, `G1 url ok`,
`G1 typeData null` still, `G1 ctx` live, no `CR2`. Keep that as a
print-only ELF; do not overwrite `g1-populate`. Next toward Wayland:
`beginCreate` only with `start=0` and url set. `typeData` may stay
null (cache-unit path uses `compilationUnit` + `start`, not TypeLoader).
Observed: `G1 start=0`, `G1 url ok`, `G1 ctx` live, `G1 typeData null`,
then `G1 beginCreate begin`, then `CR2=0xC`. **start=0 does not fix
beginCreate.** Do not retry `beginCreate` / `completeCreate` on this
empty-component + attached CU. Restore `desktop.elf.g1-start0`.
Daily ISO remains `g1-populate`. Object create still needs type
resolution (`typeData`); TypeLoader/`loadUrl` is a dead path on this
guest. Do not unskip Wayland. Approach 2 (compositor-first) is
**not** `BFREE_BOOT_GUI_FIRST=1` on the daily kernel: missing
`compositor.elf` drops PID1 to `shell.elf` and kills the FB desk.
Keep `g1-start0`. Host path is `make -C gui_server` then
`tron_gui_server` (Linux), not guest `desktop.elf`. Guest
`compositor.elf` is a separate ABI port (COMPOSITOR role ≠ musl
AF_UNIX). Host hello-world observed: `tron_gui_server` +
`wayland-info` → `interop check: PASSED` (`wl_compositor` v4,
`xdg_wm_base`, `wl_seat` bfree-seat0, 1024×768). That is Linux
host compositor, not guest `desktop.elf` / not QEMU. Guest H
path: `userland/compositor_stub` → `compositor.elf` (syscall 24
serial hello, then FB magenta fill via mmap). First proof does
**not** rebuild the kernel and does **not** set
`BFREE_BOOT_GUI_FIRST=1`. Clone daily `bfree.iso` to
`bfree-compositor-stub.iso` with
`tools/build_compositor_stub_iso.sh`: same `g1-desk` kernel,
GUI menuentry loads `init_tramp.elf` as `init.elf` and
`compositor.elf` as its own module. Trampoline `exec_initrd`
sets APP so `vfork`+pipe work (INIT cannot `fork`; Linux 57
is not on the native switch). Parent is the server, child is
the client (shared AS). Success
serial is `[compositor] guest stub hello` then
`guest stub fb fill` (E820 count 1). Observed: QEMU full-screen
magenta on `bfree-compositor-stub.iso` after GRUB rewrite of
all `desktop.elf` menuentries (daily ISO has several; requiring
exactly one rewrite left a byte-identical copy). Magenta fill is compositor owning the framebuffer. Next guest
slice is in-process Wayland wire (`get_registry` / `bind` /
`create_surface` / `create_pool` / `create_buffer` / `attach` /
`commit`) plus a cyan `wl_shm` rectangle on that magenta
clear. Observed after magenta: `wl shm mmap=0x3c00000` then
`#GP` vector `0xD` at `movaps` (`bytes@RIP=0f 29 …`).
Heap mmap worked; GCC `-O2` emitted SSE because the stub
Makefile lacked `-mno-sse`. Build with
`-mno-sse -mno-mmx -mno-3dnow -mno-80387`. Observed after
that: QEMU magenta clear + centered cyan rectangle with a
white edge (in-process `wl_shm` commit blit). That is guest
Wayland **wire + shm pixels**, not a second client ELF, not
`WAYLAND_DISPLAY` / AF_UNIX, not host `tron_gui_server`, not
the icon desk. Two-process path: `init_tramp.elf` as PID1
then APP `vfork` (child writes Wayland bytes on a pipe, parent
dispatches + blits). If `vfork` fails, in-process fallback.
Observed: `[init] exec compositor.elf`, `wl vfork=0` +
`[wl] vfork child`, `wl vfork=0x2` + `[wl] vfork parent`,
then `get_registry`…`commit` and `wayland shm blit`. No
`PANIC`. Desk chrome is a 15-tile + taskbar `wl_shm` blit
(not product `DesktopShell.qml`). A 1024×768 / 3MiB **BSS**
hung `load_elf`. Full FB uses **anonymous mmap** (Linux nr 9)
after the magenta fill. Transport is now **AF_UNIX**
`/tmp/wayland-0` (bind 49 / listen 50 / connect 42 / accept 43),
not a pipe. vfork child `connect()`s; in-process connect is
the fallback. Success serial: `[wl] listen ok` then
`[wl] client accepted` then `wayland native desk blit`.
After that blit the stub used to hang with **no pointer**. Daily
FB desk used a software **crosshair** (`guest_desk_draw_cursor`).
Host `DesktopShell.qml` never shipped a custom sprite — Qt
`ArrowCursor` is the Ubuntu/Yaru (or Adwaita) **system** pointer.
Linux vs Windows arrows differ by theme, not by kernel ABI.
The stub now paints an X11 `left_ptr` / Windows-style arrow
(hotspot tip) via `sys_poll_input_event` (nr 0, BSS slot):
`[wl] cursor arrow`, then type=3 mouse moves it. Icon click
paints a lookalike window on the compositor FB (`[wl] desk open`).
The Wayland client now sends **two `xdg_toplevel`s** from **p8test.elf**
(g1-desk named module, **not** `desktop.elf`):
fullscreen desk chrome (icons + Start bar, title `bar`) and a
480×320 app window. Compositor `vfork` (Linux nr **58**) then
`execve("/p8test.elf")` with `QT_QPA_PLATFORM=wayland` (no bfree inject —
that is only for `desktop.elf`). Do **not** `fork` (nr 57) the compositor:
AS-copy COWs the hardware FB (solid wallpaper, `[COW] break` on every
mouse, unusable). First-frame clients **exit after shm/wire** so the
vfork parent can blit. A live Qt `exec()` loop still cannot share the
FB this way. `hello.elf` is the fallback if p8test
execve returns. Cloud / trees without the guest Qt prefix still map the
C `qt_wl_client.elf` as p8test (serial `[qt] p8test.elf wayland client`).
When `bash tools/build_qt_wl_hello.sh` can link, `qt_wl_hello.elf`
(real `QGuiApplication` + stub QPA key `wayland`, **not** `libqbfree.a`,
**not** `guest_link_compat` `QT_QPA_PLATFORM=bfree` as the selected
platform — argv is `-platform wayland`) replaces that mapping. That QPA
emits the same canned wire + `/tmp/wlXX` tiles; it is **not** upstream
qtwayland. Qt window proof: gold title bar `0xD4A017` + navy body
`0x1E3A8A` + cyan mark `0x06B6D4` (C client stays green/`shm`/rose).
Success serial (C slot): `[wl] vfork=` then `[wl] execve p8test.elf`,
`[qt] p8test.elf wayland client`, `[qt] p8test.elf shm`,
`[wl] client shm blit`, `[compositor] xdg-shell window`.
Success serial (QGuiApplication slot): `[qt] QGuiApplication start`,
`[desktop_qt] plugin ctor bss stack bump`, `[qt] plugin registered`,
`[desktop_qt] musl malloc preflight OK`, `[qt] before QGuiApplication ctor`,
`[qt] QGuiApplication ctor ok`, `[qt] QPA wayland create`,
`[qt] QGuiApplication flush`, `[qt] QGuiApplication shm`,
`[qt] QGuiApplication wire`, `[wl] vfork parent`, `[wl] client shm blit`.
Do not print `wl fork=` (that was the unusable AS-copy path).
Do not print `wl execve p8test=` (that means exec returned). Do not print
`[wl] shm magic miss` on the success path. Cursor, Start panel, and
lookalike apps stay **software FB** overlays
(5×7 glyphs, no GPU, no Qt scene graph). The bar/icons themselves
are the fullscreen xdg desk surface, not extra compositor paint.
Host `DesktopShell.qml` Start/Explorer are QML; this stub is not
that. Do **not** polish the lookalike Explorer as the product —
practical file UI needs guest **Qt** (`desktop.elf` as a Wayland
client). Do **not** port that as GTK. Do **not** `execve("desktop.elf")`
on `g1-desk` (unknown names become busybox, or `QT_QPA_PLATFORM=bfree`
steals FB). Do **not** replace daily `bfree.iso` with the stub ISO
until that Qt client can run; making 仮 chrome into 本デスク first
is the compositor socket + shm path, not more FB widgets.
Terminal writes from the **top**; when the client fills, old lines
scroll off the top and a right-edge scrollbar brings them back.
Do not pin the `# ` prompt to the empty bottom. Enter runs busybox
(`ls /`, `echo hi`). Magenta FB fill was S0 proof-of-life; leaving
it under the desk flashes on present (cursor under / full blit).
Fill/restore with the desk wallpaper color and dirty-rect blit
from shm. Terminal/Explorer `vfork`+pipe+`execve("/busybox.elf")`
and paint captured stdout. That is a
real busybox process, not `desktop.elf`. From-source kernel on the
stub ISO still kills QEMU. 本デスク still needs a **bootable**
kernel that execs `desktop.elf` as a Wayland client (not bfree QPA).
Phases / TODOLIST: `bfree_x86_64/docs/HONDESK_TODOLIST.ja.md` (W8 current).
S0–S2 history: `bfree_x86_64/docs/HONDESK_PHASES.ja.md`. QEMU hides the
host cursor when grabbed (`Ctrl+Alt+G`); the guest must paint
its own. Still not `desktop.elf` / not product QML.
That is S1 toward 本デスク (compositor owns the socket).
S2 source whitelist (`desktop.elf` in `sys_linux_execve`) may
live in `syscall.c`, but **do not** `make -C kernel` and map
that ELF onto the stub ISO. Observed: from-source
`kernel.elf.s2-execve` + `-no-reboot` → QEMU exits immediately,
serial empty (no `[init]`). That is the same class as the
VMM/`sparse-pt` hang. Stub ISO must keep the daily `g1-desk`
kernel. Never overwrite `kernel.elf.g1-desk` or daily
`bfree.iso`. Do not `execve("desktop.elf")` from the stub
until a bootable patched kernel exists. The Wayland client on
`g1-desk` is **p8test.elf**: compositor `vfork`(58)+`execve("/p8test.elf")`
with `QT_QPA_PLATFORM=wayland`. Not `desktop.elf`. Not compositor
`fork`(57) — that COWs the FB. C `qt_wl_client.elf`
is the default mapping; `qt_wl_hello.elf` (`QGuiApplication` + stub
QPA) replaces it when the guest Qt prefix can link. `hello.elf` is
fallback. Cloud cannot link `QGuiApplication` (no `libQt6Gui.a` /
`crt0.o` / `desktop.ld`). On the maintainer tree,
`tools/build_qt_wl_hello.sh` must pass `-D__linux__` because
`x86_64-elf-g++` is not a Linux target (`qsystemdetection.h` otherwise
errors "Qt has not been ported to this OS"). Same define as
`Makefile.guest-elf`. The stub QPA must **not** call
`createUnixEventDispatcher()` (undefined on this static guest Qt /
`threads=no` libstdc++). It uses a local `QAbstractEventDispatcher`
and a no-op `processEvents` then `return 0`. A failed Qt link
writes `userland/compositor_stub/qt_wl_hello.link.log` and keeps C
p8test; do not treat `P8_KIND=C p8test` as a script crash.
Invoke the linker via a CR-stripped copy in `/tmp` (not
`guest_desktop_link_qmake.sh` / `bash -s`). Failed-link log is `/tmp/qt_wl_hello.link.log`. A 204-byte log
that is only `[guest_desktop_link] start` means `set -e` hit
`((i++))` when `-o` is argv[0] (expression value 0). Use
`i=$((i + 1))`. Do not link `guest_platform_stub.o` into
`qt_wl_hello.elf` (that object is the bfree QPA factory
`QPlatformIntegrationPluginBFree` / `libqbfree.a`). Provide a dummy
`__real_qInitResources_guest_desktop` instead of desktop qrc.
`guest_link_compat` `getenv` still reports `QT_QPA_PLATFORM=bfree`;
the hello QPA must accept key `bfree` or QGuiApplication aborts
(`abort()` wrap = infinite pause, wallpaper forever). Stub QPA keys:
`wayland`, `bfreewl`, `bfree`. Restore C p8test with
`BFREE_P8TEST_C=1 bash tools/build_compositor_stub_iso.sh` (keep the
53MB ELF). 53MB `p8test` exec is slow; gray wallpaper with no desk
means the Qt child printed `[qt] QGuiApplication start` and never
returned. g1-desk `vfork` waits for **exit**, so the compositor
parent never paints (`wl vfork=0` is the child; `[wl] vfork parent`
does not appear). Do **not** call `qRegisterStaticPluginFunction` or
construct `QGuiApplication` on the exec stack (`0x13xxxxxx`); that is
the first Qt heap and PFs / hangs before `plugin registered`. Use the
same bring-up as `desktop.elf`: `bfree_guest_refresh_libc_auxv`,
`bfree_guest_run_on_ctor_stack_plugins` (BSS+bump),
`bfree_guest_preflight_musl_heap`, `bfree_guest_preflight_ctor_mmap`,
then `bfree_guest_run_on_ctor_stack_hybrid` for ctor+paint+flush.
Hello **must return 0** after first frame. Do **not**
`bfree_guest_enter_preflighted_mmap_noreturn` (never returns, parent
never blits). Do **not** `bfree_guest_install_static_env` in hello.
Grep empty for `plugin registered` / `ctor ok` / `vfork parent` after
`start` is that hang. After the ctor-stack patch expect
`plugin ctor bss stack bump` then `plugin registered`. Restore C desk:
`BFREE_P8TEST_C=1 bash tools/build_compositor_stub_iso.sh`.
`[desktop_qt] abort()` means the platform plugin was not found.
Stub window drag looks jagged and Terminal Enter is slow: software
FB dirty blit + per-Enter `vfork`/`busybox.elf`. Do not polish that
as the product. `threads=no` libstdc++ warning is expected. Do **not** drop `QT_QPA_PLATFORM=bfree` on
daily `bfree.iso` until that stub is the boot
desk. Stub ISO: `BFREE_ISO` may be `bfree-desk.iso` when daily
`bfree.iso` is absent. Not product
`DesktopShell.qml`. Daily `bfree.iso` still has the
FB icon desk. Never overwrite daily `bfree.iso`. COMPOSITOR allowlist
includes nr 24 for a later GUI_FIRST kernel; do not build that
into daily `kernel.elf`. A from-source GUI_FIRST kernel hung
at VMM/`sparse-pt` here; do not retry that as the hello path.
Do not `PROFILE=RELEASE` (`BFREE_WAYLAND_INPUT_STRICT=1`
starves APP input). If `tools/converge_guest_resource_holder_va.sh`
is missing locally, print `nm` holder vs `HOLDER_VA` and ISO
only when they match; do not loop-rebuild on a match.
A skip-create (or any `guest_main` relink) ISO that shows only
`CR2=8` and no `skip DesktopShell` / HIT is a holder VA miss
at STAGE 5, not a `qmlData` read fault. Restore
`desktop.elf.good-running` first. Then
`tools/converge_guest_resource_holder_va.sh` until `nm`
equals the header before `build.sh`. Do not ISO until
`[ok] holder converged`. The converge script must not
delete `desktop.elf`; if `desktop` is under 10MB it is a
stub (`Qt guest not linked`). Restore
`desktop.elf.good-running` and do not `build.sh`. Always
pass `BFREE_QT_GUEST_LINKED=1 BFREE_MVP_GUEST_QML=1`.
Do not run `build_iso_desktop_shell.sh`.
