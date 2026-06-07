# clawd-meter

> A pixel-art desk widget for the **ESP32 "Cheap Yellow Display"** that shows
> your **Claude usage** as glanceable channels — and gives it a face: **Clawd**,
> an animated mascot whose mood follows how much of your quota is left.

clawd-meter is a Claude-only remix of [glimmer](https://github.com/Avinava/glimmer)
(the usage dashboard + channel engine) combined with the mascot idea from
[clawd-mochi](https://github.com/yousifamanuel/clawd-mochi) (the animated eyes).
Codex support has been removed; everything is focused on Claude + Clawd.

---

## What it shows

| Channel | What |
|---|---|
| **Clawd** | Animated mascot. In *auto* mode the expression follows your Claude 5-hour usage (relaxed → normal → stressed → sleepy). In *manual* mode you pick the expression in the web UI. |
| **Home** | Clock + weather + both Claude windows (5-hour "5H" and weekly "7D") + 24-hour timeline |
| **Claude usage** | 5-hour window % + weekly % + reset countdowns + per-model breakdown |
| **Clock** | Big VT323 digital clock, day-of-week, greeting |
| **Weather Now / 5-day Forecast** | Open-Meteo, no key needed |
| **Info** | IP / SSID / signal / uptime / heap / CPU / firmware |
| **Push cards** | One-shot notification cards via `POST /push` |

All channels use region-based partial repaints — no flicker between updates.

## Hardware — ESP32 "Cheap Yellow Display" (ESP32-2432S028R)

- **MCU**: ESP32-WROOM-32, dual-core 240 MHz, 4 MB flash
- **Display**: 320×240 ILI9341 (landscape), backlight active-high PWM on GPIO21
- **Connectivity**: Wi-Fi 2.4 GHz; USB is data-wired (flash directly over USB)
- **Touch**: XPT2046 resistive — tap the screen to advance to the next channel (toggle in the web UI)
- microSD slot present but unused

## Quick start

```bash
git clone https://github.com/<you>/clawd-meter.git
cd clawd-meter
pio run -e cyd -t buildfs   # LittleFS image (fonts + web UI)
pio run -e cyd              # firmware
pio run -e cyd -t upload    # flash firmware over USB
pio run -e cyd -t uploadfs  # flash filesystem over USB
```

On first boot the device opens a Wi-Fi AP **`glimmer-setup`** → visit
`http://192.168.4.1/` to enter your home Wi-Fi and your Claude session key.
Afterwards the web UI is at `http://<device-ip>/`.

> `-t uploadfs` rewrites the whole LittleFS partition, which wipes
> `/config.json`. To keep your settings across a filesystem update, back up
> first (`curl http://<device-ip>/api/export`) and restore via the setup AP
> (`POST /api/import`).

## Web interface

- **Tokens** — your Claude `sessionKey` cookie (claude.ai → DevTools → Cookies).
- **Channels** — toggle which screens rotate (incl. the Clawd mascot), auto-rotate, and tap-to-advance (touch).
- **Clawd** — mode (auto/manual), manual expression, animation speed, eye &
  background color.
- **You** — brightness, highlight color, consumed-vs-remaining usage, timezone,
  night-dim, personalization.

## Clawd moods (auto mode)

Driven by your Claude **5-hour** window:

The eyes always use your chosen eye color; the *shape* conveys the mood:

| 5-hour usage | Expression |
|---|---|
| lots left (< 40% used) | happy ( ^ ^ ) |
| good (40–60% used) | squish ( > < ) |
| mid (60–85% used) | normal eyes (double-blink + wiggle) |
| near limit (≥ 85% used) | stressed (worried brows) |
| no data yet | sleepy |

## Getting your Claude token

1. Open DevTools on `claude.ai` → **Application → Cookies → `https://claude.ai`**
2. Copy the `sessionKey` value (starts with `sk-ant-sid02-…`)
3. Paste it on the web UI **Tokens** page.

> The `sessionKey` is read by replaying your own browser session against the
> private endpoint claude.ai uses for its own dashboard. It is undocumented and
> not part of a public API — see the Disclaimer.

## Credits

Built on two MIT-licensed projects — thank you to both:

- **[glimmer](https://github.com/Avinava/glimmer)** by Avinava — the usage
  dashboard, channel engine, web UI and TFT_eSPI rendering this is forked from.
- **[clawd-mochi](https://github.com/yousifamanuel/clawd-mochi)** by Yousuf
  Amanuel — the animated Clawd mascot / expression idea, re-implemented here on
  TFT_eSPI (see `src/channels/ch_clawd.cpp`).

Clawd is the pixel-crab mascot from Claude Code by Anthropic. This is an
independent fan project — not affiliated with, sponsored by, or endorsed by
Anthropic.

## Disclaimer — personal & educational use only

clawd-meter reads your own Claude usage by sending **your own credentials**
(a `sessionKey` cookie) to an internal endpoint claude.ai uses to power its web
UI. **That endpoint is undocumented and not part of a public API.** It can
change without notice, and accessing it programmatically may be inconsistent
with Anthropic's Terms of Service depending on interpretation. You are
responsible for your own ToS compliance, your devices/accounts/network, and for
securing your credentials (stored on the device's LittleFS in plaintext). Shared
as-is, no warranty. Do not redistribute as a commercial product; do not access
accounts that are not yours.

## License

MIT — see [LICENSE](./LICENSE). The bundled fonts (VT323, Silkscreen, DM Mono,
Pixelify Sans) are under the SIL Open Font License.
