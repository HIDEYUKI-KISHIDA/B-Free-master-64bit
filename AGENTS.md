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
(type-loader never ran — this loop skips `processEvents` on purpose).
Do not reintroduce a boot URL ctor to “try again”.
