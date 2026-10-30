# BLE Scanner and Host mode

Angularity can act as a Bluetooth LE **host** (GAP central / GATT client) as well as the usual
**peripheral** that phones and computers connect to.

## Modes

Settings > Bluetooth > **Mode**

| Mode | What it does |
|---|---|
| Peripheral | Default. The Flipper advertises; phones, qFlipper and HID apps connect to it. |
| Host | The Flipper never advertises. Apps can scan for and connect to other devices. The status bar shows the Bluetooth icon with an **H**. Phone apps cannot connect while in Host mode (so no Bluetooth updates from the phone). |

The mode is saved. `bt_profile_restore_default()` returns to the profile of the current mode.

**Requirement:** Host mode needs the *full* BLE radio stack (central role, GATT client). The
firmware default is still the stock *light* stack, which is peripheral only; on it the Mode row
reads "Peripheral only" and the BLE Scanner shows "Not available". The full stack sits at
0x080CE000, so the firmware image must be <= 843,776 bytes to use it
(`COPRO_STACK_BIN` / `COPRO_STACK_TYPE` in `fbt_options.py`).

## BLE Scanner (Apps > Bluetooth)

* Scan list: strongest signal first (Left switches to first-seen order), Right switches
  passive/active scanning, OK opens a device.
* Device: name, address, signal, manufacturer, services, advertised data. OK connects.
* Services: every service and characteristic of the connected device with its properties
  (R read, W write, w write without response, N notify, I indicate). OK opens a
  characteristic, Right starts pairing, Left/Back disconnects.
* Characteristic: Left reads, OK listens for notifications, Right writes (pick the length, then
  edit the hex bytes).

Reading things that need a bond answers "pair first": go back to the service list and press
Right to pair.

## API for your own apps

```c
#include <bt/bt_service/bt.h>
#include <profiles/host_profile.h>

Bt* bt = furi_record_open(RECORD_BT);
FuriHalBleProfileBase* host = bt_host_start(bt);      // NULL if the radio stack can't do it
ble_profile_host_set_event_callback(host, on_event, ctx);
ble_profile_host_scan_start(host, /*active*/ false, /*filter duplicates*/ false);
// ... BleHostEventScanResult events; then ble_profile_host_connect(), _discover(), _read(),
//     _write(), _subscribe(), _pair() ...
bt_host_stop(bt);                                      // host is invalid afterwards
```

`profiles/host_profile.h` documents every call and event. The event callback runs on the BLE
event thread (small stack): record what you need and return. `furi_ble/host_adv.h` parses
advertising data and is safe on untrusted input.

## Tests

`tests/run.sh` builds the pure parts (advertising parser, ATT response decoders, discovery
walker, name tables) on a computer with AddressSanitizer/UBSan and runs them, including fuzzing
with hostile input. The radio-facing code in `host_profile.c` cannot be run without a Flipper.
