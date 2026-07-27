/*
 * Boot BusyBox ash regress via Linux process ABI (stack+auxv → e_entry).
 */
#include "ash_regress_guest_boot.h"
#include "elf_user_load.h"
#include "initramfs.h"
#include "linux_user_stack.h"
#include "user_boot.h"

#include <stddef.h>
#include <stdint.h>

#define ASH_GUEST_BUSYBOX_PATH "bin/busybox"

/* Keep in sync with markers expected by qemu_ash_regress_guest_smoke.sh */
static char ash_regress_cmd[] =
	"out=$(echo pipe-data | cat); [ \"$out\" = \"pipe-data\" ] || exit 1; "
	"echo ASH_PIPE_GUEST_OK; "
	"marker=parent; ( marker=subshell ); [ \"$marker\" = \"parent\" ] || exit 1; "
	"echo ASH_SUBSHELL_GUEST_OK; "
	"out=$(echo hello); [ \"$out\" = \"hello\" ] || exit 1; "
	"echo ASH_CMDSUBST_GUEST_OK; "
	"sleep 0 & wait; echo ASH_BG_GUEST_OK; "
	"/bin/true || exit 1; /bin/false && exit 1; echo ASH_EXTERNAL_GUEST_OK; "
	"date +%s >/dev/null || exit 1; echo ASH_DATE_GUEST_OK; "
	"[ \"$(id -u)\" = 0 ] || exit 1; echo ASH_ID_GUEST_OK; "
	"ln -s /bin/true /tmp/ash_rl && [ \"$(readlink /tmp/ash_rl)\" = /bin/true ] || exit 1; "
	"echo ASH_LN_READLINK_GUEST_OK; "
	"stat / >/dev/null || exit 1; echo ASH_STAT_GUEST_OK";

int bfree_ash_regress_guest_boot(void)
{
	const void *payload;
	size_t payload_len;
	uintptr_t entry;
	uintptr_t rsp;
	struct bfree_linux_auxinfo aux;
	char *argv[] = {
		"/bin/busybox", "ash", "-c", ash_regress_cmd, NULL
	};

	if (bfree_initramfs_lookup(ASH_GUEST_BUSYBOX_PATH, &payload,
				   &payload_len) != 0)
		return 0;
	if (bfree_user_elf_install_ex(payload, payload_len, &entry, &aux) != 0)
		return 0;

	rsp = bfree_linux_user_stack_build(BFREE_USER_STACK_TOP, 4, argv, NULL,
					   &aux);
	if (rsp == 0)
		return 0;

	bfree_user_boot_exec(entry, rsp);
	return 1;
}
