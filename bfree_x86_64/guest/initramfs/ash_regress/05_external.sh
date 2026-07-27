#!/bin/busybox ash
# ASH_EXTERNAL — external binary via PATH (not NOFORK inline applet).
set -eu
/bin/true || { echo "ASH_EXTERNAL: FAIL (/bin/true)"; exit 1; }
/bin/false && { echo "ASH_EXTERNAL: FAIL (/bin/false should fail)"; exit 1; }
echo "ASH_EXTERNAL: PASS"
