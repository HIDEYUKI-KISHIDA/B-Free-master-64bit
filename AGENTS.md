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
`tools/rebuild_guest_qtdeclarative_singlethread.sh`. If
`libQt6Qml.a` still has `QQmlThreadPrivate::threadEvent`, the guest
Qt is the threaded loader and `loadUrl` will stay Loading. Apply the
single-thread stub (or force `isThisThread()` true) and rebuild Qml
before retrying `getType`. Do not reintroduce a boot URL ctor.
