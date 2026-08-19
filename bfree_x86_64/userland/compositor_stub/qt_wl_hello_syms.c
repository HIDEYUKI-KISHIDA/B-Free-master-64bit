/* Linked only into qt_wl_hello.elf. guest_link_compat.o (desktop.elf wrap)
 * calls __real_qInitResources_guest_desktop; this hello has no guest qrc.
 * Do not pull guest_platform_stub.o / libqbfree.a. D2 embeds
 * compositor_stub/DesktopShell.qml as a string; do not beginCreate it. */
void __real__Z28qInitResources_guest_desktopv(void) {}
void _Z28qInitResources_guest_desktopv(void) {}
