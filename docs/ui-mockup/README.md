# Smart Boiler — UI Redesign Mockup

Interactive HTML mockup of the redesigned master touchscreen UI (ESP32-S3, 800×480, LVGL).
Open [`index.html`](index.html) in any browser — no build step, no hardware needed.

This is a **design prototype**, not firmware. It defines the target look, layout, and
navigation before the screens are ported into LVGL (`master/src/ui_manager.cpp`).

## Why this redesign

Three problems with the original single-screen UI drove this work:

1. **WiFi entry was messy** — a cramped modal where the "save password" checkbox overlapped the keyboard.
2. **Main UI was inefficient** — everything fought for space on one screen.
3. **Touch targets were too small** for the 5" panel.

## Design decisions

- **Navigation:** Home + tiles. The home dashboard holds the daily essentials; big tiles open focused sub-screens with a back button. Transitions are a smooth fade + slide.
- **Language:** English (matches current firmware).
- **Audience:** consumer-first, plus a hidden **Diagnostics** page (opens via long-press on device) showing raw sensor/PLC/SSR values for development.
- **Touch targets:** minimum 64px everywhere.

## Screens

| Screen | Purpose |
|--------|---------|
| **Home** | Water-temp circle, big ON/OFF, shower-status strip, `Auto` pre-heat badge, nav tiles |
| **Settings** | Large 92px −/+ target-temp control; links to Schedule and Diagnostics |
| **Schedule** | `Smart learn` vs `Ready by` time (per weekday/weekend), learning progress, next weather-adjusted pre-heat — per [`docs/smart-brain.md`](../smart-brain.md) §7 |
| **Network** | Full-screen WiFi list + password screen |
| **Stats** | Usage metrics + 24h "when you shower" histogram (the brain's learning data) |
| **Diagnostics** | Hidden raw-data grid for development |

## WiFi password flow (key change)

The old checkbox is **gone**. Instead:

- The keyboard fills the freed space (keys ~62px tall — important on the small screen).
- A show/hide eye toggle was added to the password field.
- **After** a successful connect, a dialog asks *"Save this password?"* (Not now / Save).
  Rationale: only offer to save a password that actually worked, and maximize keyboard size.

> Porting note: this means credential-saving moves out of the connect handler
> (currently `ui_manager.cpp` saves on the checkbox state) to *after* `WL_CONNECTED`.

## Status

Design phase. Next step (when the device is available): port these screens into LVGL using a
reusable component kit (`make_big_button` / `make_card` / `make_nav_bar`) + a screen manager.
