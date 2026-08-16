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
- **Never-committed build inputs** (gitignored or simply absent):
  `bfree_x86_64/multiboot2_header.S`, `bfree_x86_64/userland/embedded/`,
  `bfree_x86_64/iso_root/` (incl. `boot/grub/grub.cfg`),
  `bfree_x86_64/userland/init/`, `bfree_x86_64/userland/busybox_guest/`, and
  `bfree_x86_64/build.sh`. Therefore the documented full ISO/QEMU flows
  (`tools/build_busybox_iso.sh`, the `tools/*_smoke.sh` scripts) do **not** run
  as-is from a clean checkout.
- `kernel/Makefile`'s default `all` target regenerates the (already committed)
  `kernel/sysmain/user_hello_elf.c` from the missing `userland/embedded/user_hello.S`
  chain and fails. The embedded hello ELF is already committed; the multiboot2
  header + `_start` already live in `kernel/sysmain/sysdepend/x86_64/reset.S`, so
  `../multiboot2_header.S` is redundant.
- The committed kernel does **not** compile/link cleanly: `syscall.c` uses
  undefined macros `BFREE_MAX_SIGNALFD` / `BFREE_SIGNALFD_FD_BASE`, and `vmm.c`
  references undefined globals `g_bfree_shell_text_fp` / `g_bfree_shell_text_fp_valid`.
- Curated tests `userland/ltp_curated` and `userland/libc_test_curated` fail to
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
then `G1 thin QML Ready`. If `qrc gui_shaders` / `CR2=8` or hang
at `G1 sync loadUrl begin`, restore the backup.
