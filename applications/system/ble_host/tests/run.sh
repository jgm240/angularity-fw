#!/bin/sh
# Builds and runs the host-mode unit tests on a computer (no Flipper needed), with
# AddressSanitizer and UndefinedBehaviorSanitizer on. Run from any directory.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../../../.."
OUT="${TMPDIR:-/tmp}/ble_host_tests"
mkdir -p "$OUT"
# AddressSanitizer is used when it works on this machine (the runtime can hang after some
# macOS / Xcode updates), otherwise only UndefinedBehaviorSanitizer. The tests also read their
# inputs through a guard page, so over-reads crash either way.
SAN="address,undefined"
printf '#include <stdio.h>\nint main(void){puts("ok");return 0;}\n' > "$OUT/probe.c"
if ! cc -fsanitize=address "$OUT/probe.c" -o "$OUT/probe" 2>/dev/null ||
    ! perl -e 'alarm 10; exec @ARGV' "$OUT/probe" >/dev/null 2>&1; then
    echo "AddressSanitizer unusable here, running with UndefinedBehaviorSanitizer only"
    SAN="undefined"
fi
CFLAGS="-std=c11 -Wall -Wextra -Werror -fsanitize=$SAN -fno-omit-frame-pointer -g"
cc $CFLAGS -I"$ROOT/targets/f7/ble_glue/furi_ble" "$HERE/test_host.c" -o "$OUT/test_host"
cc $CFLAGS -I"$HERE/.." "$HERE/test_names.c" -o "$OUT/test_names"
"$OUT/test_host"
"$OUT/test_names"
