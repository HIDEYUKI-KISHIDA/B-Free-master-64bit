#!/usr/bin/env bash
grep -E 'build=|QML ready|QQmlEngine ok|bridge|EXCEPTION|smoke]|TIMEOUT|futex|nanosleep|memcpy' /tmp/v153-smoke.log 2>/dev/null || true
f=$(ls -t /tmp/bfree-guest-smoke.* 2>/dev/null | head -1)
echo "LOG=$f"
grep -E 'build=|QML ready|QQmlEngine ok|bridge|qv4|EXCEPTION|event loop|memcpy|futex|nanosleep|readlink|realpath' "$f" | tail -80
