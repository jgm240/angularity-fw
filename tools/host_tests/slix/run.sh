#!/bin/sh
# Compiles the real SLIX / ISO15693 listener sources against small shims and fuzzes them.
# Usage: run.sh [requests per tag variant] [seed]. Exit status 1 when a furi_check can be hit.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../../.."
LIB="$ROOT/lib"
OUT="${TMPDIR:-/tmp}/slix_fuzz"
mkdir -p "$OUT"
SRC="
$LIB/nfc/protocols/slix/slix.c
$LIB/nfc/protocols/slix/slix_i.c
$LIB/nfc/protocols/slix/slix_listener.c
$LIB/nfc/protocols/slix/slix_listener_i.c
$LIB/nfc/protocols/iso15693_3/iso15693_3.c
$LIB/nfc/protocols/iso15693_3/iso15693_3_i.c
$LIB/nfc/protocols/iso15693_3/iso15693_3_listener.c
$LIB/nfc/protocols/iso15693_3/iso15693_3_listener_i.c
$LIB/toolbox/bit_buffer.c
$LIB/toolbox/simple_array.c
$LIB/nfc/helpers/iso13239_crc.c
$HERE/stubs.c
$HERE/slix_fuzz.c
"
cc -std=gnu11 -O1 -g -w -fsanitize=undefined -fno-sanitize=alignment -I"$HERE/shim" -I"$LIB" -I"$ROOT" -I"$LIB/mlib" $SRC -o "$OUT/slix_fuzz"
"$OUT/slix_fuzz" "$@"
