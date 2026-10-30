# SLIX / ISO15693 emulation fuzzer

Compiles the real listener sources (`lib/nfc/protocols/slix`, `iso15693_3`, `bit_buffer`, ...)
against small shims of Furi on a computer and sends them millions of requests, for every SLIX
variant plus tags whose UID is not recognised. Every `furi_check()` that a request can trigger
is reported with the request bytes that caused it.

    tools/host_tests/slix/run.sh [requests per tag variant] [seed]

Exit status 1 means a request can still crash the emulation. It also runs a few named
regression cases first (see `regressions()` in `slix_fuzz.c`).

Found with it:

* A request with the reserved command byte `0x00` indexed the ISO15693 mandatory-command
  table at `-1`. On the Flipper that reads the field before the table, which is
  `iso15693_3_listener_run`, and calls it with the wrong arguments (crash / `furi_check`).
* A SLIX tag whose UID is not recognised as a SLIX variant (for example after editing the UID)
  hit `furi_check(slix_type < SlixTypeCount)` as soon as a reader sent Set Password,
  Write Password or Enable Privacy, and showed a garbage device name.

Limits: this exercises the protocol code, not the radio, timing or the NFC app.
