#!/bin/busybox ash
# ASH_DATE — date applet via gettimeofday/clock_gettime (epoch printable).
set -eu
out=$(date +%s)
# Tick-based guest clock may be near 0; require a non-empty decimal.
case "$out" in
	''|*[!0-9]*)
		echo "ASH_DATE: FAIL (got '$out')"
		exit 1
		;;
esac
echo "ASH_DATE: PASS"
