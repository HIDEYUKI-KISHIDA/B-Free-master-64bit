#!/usr/bin/env python3
"""Replace BusyBox df_main with a guest-safe synthetic implementation.

musl getmntent/fgets crashes in the B-Free guest (memchr on NULL FILE buffer).
"""
import pathlib
import sys

p = pathlib.Path(sys.argv[1])
t = p.read_text()
if "B-Free: synthetic df" in t:
    print("[busybox] df.c already patched")
    raise SystemExit(0)

# Force NOFORK so ash does not re-exec
t2 = t.replace(
    "//applet:IF_DF(APPLET_NOEXEC(df, df, BB_DIR_BIN, BB_SUID_DROP, df))",
    "//applet:IF_DF(APPLET_NOFORK(df, df, BB_DIR_BIN, BB_SUID_DROP, df))",
    1,
)
if t2 == t and "APPLET_NOFORK(df," not in t:
    # Already may have been NOEXEC with different spacing
    pass
t = t2

marker = "int df_main(int argc UNUSED_PARAM, char **argv)\n{"
idx = t.find(marker)
if idx < 0:
    marker = "int df_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;\nint df_main(int argc UNUSED_PARAM, char **argv)\n{"
    idx = t.find("int df_main(int argc UNUSED_PARAM, char **argv)\n{")
if idx < 0:
    sys.stderr.write("df_main not found in %s\n" % p)
    raise SystemExit(1)

# Find end of df_main: matching closing brace before final #endif-ish — use last "return status;" block
# Safer: replace whole function body from opening { of df_main to the line "return status;\n}"
start = t.find("{", idx)
if start < 0:
    raise SystemExit("df_main body missing")

# Find matching closing brace for df_main by brace count
depth = 0
end = None
i = start
while i < len(t):
    c = t[i]
    if c == "{":
        depth += 1
    elif c == "}":
        depth -= 1
        if depth == 0:
            end = i + 1
            break
    i += 1
if end is None:
    raise SystemExit("df_main end not found")

new_fn = r'''int df_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	/* B-Free: synthetic df — avoid musl getmntent/stdio crash in guest */
	printf("Filesystem           1024-blocks    Used Available Use%% Mounted on\n");
	printf("%-20s %9lu %9lu %9lu %3u%% %s\n",
		"rootfs", 524288UL, 131072UL, 393216UL, 25U, "/");
	printf("%-20s %9lu %9lu %9lu %3u%% %s\n",
		"proc", 0UL, 0UL, 0UL, 0U, "/proc");
	printf("%-20s %9lu %9lu %9lu %3u%% %s\n",
		"tmpfs", 524288UL, 131072UL, 393216UL, 25U, "/tmp");
	return EXIT_SUCCESS;
}'''

# Keep the MAIN_EXTERNALLY_VISIBLE declaration if present just before
# Our replacement starts at "int df_main(int argc UNUSED_PARAM..."
# Find the actual start of the definition line
line_start = t.rfind("\n", 0, idx) + 1
# If previous line is the MAIN_EXTERNALLY_VISIBLE prototype, keep it
proto = "int df_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;\n"
# Replace from line_start of df_main definition through end
# idx points to "int df_main(int argc UNUSED_PARAM..."
t = t[:idx] + new_fn + t[end:]
p.write_text(t)
print("[busybox] patched df.c with synthetic df")
