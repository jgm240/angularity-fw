# Standby

A dimmed bedside/desk display. Open it from **Apps > Tools > Standby** (or add the Standby tile to the
Control Center). Left/Right switch pages, Up/Down change the idle brightness, **OK** opens the menu,
Back exits. The first key press while dimmed only wakes the screen.

| Page | Shows |
|---|---|
| Clock | time, date, battery, weather line, seconds bar; "Update: releaseNN" alternates in when a newer firmware exists |
| Weather | city, temperature, condition, humidity, wind, 3-day outlook |
| Calendar | month grid, today highlighted, dots on days with JWLink events |
| JWLink | watch battery, steps, heart rate, next events, "updated Nm ago" |

## WiFi Devboard features
Weather, the clock sync and the update check use the WiFi Devboard (FlipperHTTP firmware; set up its
WiFi in **Angularity Update > WiFi setup**). Everything else works offline, and the last weather is
cached on the SD card.

- **Weather:** Open-Meteo (no account). The city is looked up by name, or from your internet location when blank.
- **Sync clock:** sets the Flipper clock from timeapi.io for your city's time zone (menu, or automatically on start).
- **Update check:** at most weekly, shares its result with the Angularity Update app.

## JWLink data (`/ext/apps_data/standby/jwlink.json`)
Written by qAngularity on your phone ("Send Standby data to Flipper"). `updated` is seconds since 1970 in the
phone's **local** time, so the Flipper (whose clock is local time) can say how old it is.

```json
{"v":1,"updated":1792165000,
 "watch":{"name":"JWatch","battery":87,"steps":4321,"heart":72},
 "events":[{"d":"2026-10-17","t":"14:30","title":"Standup"},{"d":"2026-10-20","t":"","title":"Dentist"}]}
```
Any field may be missing. Up to 6 events are read; an empty `t` means all day.
