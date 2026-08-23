# Computer Mode ESP32 firmware (EXPERIMENTAL)

This **replaces** FlipperHTTP on your WiFi Devboard's ESP32-S2 with a
completely different firmware: instead of being a WiFi-to-UART HTTP proxy,
it becomes a USB host that reads an external keyboard/mouse and relays
their input to the Flipper over the same UART connection FlipperHTTP used.

**I have not been able to test this against real hardware in any way.**
Everything about the USB Host HID driver setup is adapted directly from
Espressif's own official example
(`esp-idf/examples/peripherals/usb/host/hid`) - that part is real, current,
verified API usage, not guesswork. What's genuinely unverified:

1. **Whether the WiFi Devboard's USB-C port supports host mode at all.**
   FlipperHTTP only ever uses that port as a USB *device* (for flashing).
   USB Host mode requires the port's D+/D- lines and ideally the CC pins to
   be wired for it. I don't have the board's schematic to confirm this -
   if the port is hardwired device-only, no firmware can make it act as a
   host, full stop.
2. Whether a plain "USB-C to USB-A OTG adapter" is enough, or whether the
   board needs something else entirely to power a bus-powered keyboard.
3. Timing/compatibility across different real keyboards and mice - the
   boot-protocol HID parsing here is standard, but "should work" and
   "works with the specific keyboard you own" are different claims.

If step 1 doesn't hold, this won't work no matter how the rest of the code
is tuned - that would need different/additional hardware, not different
firmware.

## What it does

- Enumerates a USB keyboard and/or mouse connected via USB Host to the
  ESP32-S2's USB-OTG port.
- Sends one line per event over UART0 (the same UART FlipperHTTP already
  uses to talk to the Flipper - see "Why UART0 with no explicit pins"
  below):
  ```
  D<code>              key down, code = USB HID keyboard usage id (decimal)
  U<code>              key up
  M<dx>,<dy>,<buttons> mouse delta + button bitmask (bit0=left, bit1=right)
  ```
- The Flipper-side `computer_mode` app (in `applications/external/` in
  this firmware fork) reads these lines and turns them into a simple
  keyboard-driven notepad with a mouse cursor overlay.

## Why UART0 with no explicit pins

FlipperHTTP's own source (`src/flipper-http/uart.cpp` in the FlipperHTTP
repo) calls `Serial.begin(baudrate)` with no pin arguments for the ESP32-S2
board target - meaning it uses whatever the board's default UART0 routing
already is. This firmware does the same thing via
`uart_set_pin(UART_PIN_NO_CHANGE, ...)`, which keeps the default routing
rather than picking GPIO numbers - so it lands on the exact same physical
pins already wired to your Flipper's GPIO header, without me having to
guess a GPIO number I can't verify.

## Build and flash

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s2/get-started/index.html)
(v5.0+), not the Arduino IDE FlipperHTTP normally uses - this is a plain
ESP-IDF project.

```bash
cd tools/computer_mode_esp32
idf.py set-target esp32s2
idf.py build
idf.py -p <PORT> flash
```

`<PORT>` is the devboard's USB-C port in *device* mode (for flashing only -
same as flashing FlipperHTTP). Flashing does not require the host-mode
question above to be resolved; only actually using a keyboard does.

## Reverting to FlipperHTTP

If this doesn't work, or you want your WiFi Devboard's other features
(FlipperHTTP-dependent apps: Net Audio Player, Claude Chat, Spotify
Remote, DolphinMusic, Flip Browser, all built earlier in this project)
back, reflash the original FlipperHTTP firmware following its own
instructions: https://github.com/jblanked/FlipperHTTP, or via the ESP
Flasher app on the Flipper itself with FlipperHTTP's official `.bin`
files for the "WiFi Developer Board (ESP32S2)" target. This firmware
being flashed doesn't damage anything - it's a normal reflash away from
being FlipperHTTP again.
