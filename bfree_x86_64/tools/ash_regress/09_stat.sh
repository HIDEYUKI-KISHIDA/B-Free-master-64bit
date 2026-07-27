#!/bin/busybox ash
# ASH_STAT — stat applet via newfstatat/stat (directory that always exists).
set -eu
# Prefer format size; fall back to default multi-line output.
sz=$(stat -c %s / 2>/dev/null || true)
if [ -n "$sz" ]; then
	case "$sz" in
		''|*[!0-9]*)
			echo "ASH_STAT: FAIL (size='$sz')"
			exit 1
			;;
	esac
else
	out=$(stat /) || {
		echo "ASH_STAT: FAIL (stat exited $?)"
		exit 1
	}
	echo "$out" | grep -qiE 'directory|size|File:' || {
		echo "ASH_STAT: FAIL (unexpected '$out')"
		exit 1
	}
fi
echo "ASH_STAT: PASS"
