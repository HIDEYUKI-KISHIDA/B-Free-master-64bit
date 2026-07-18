#!/usr/bin/env bash
grep -n 'write error' /root/src/busybox/shell/ash.c 2>/dev/null | head
grep -n 'popredir\|openredirect\|dup2\|dup(' /root/src/busybox/shell/ash.c 2>/dev/null | head -50
