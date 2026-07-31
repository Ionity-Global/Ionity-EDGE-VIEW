# Stream Protocol

The EDGE-VIEW runs a TCP server on port **4242** that accepts JSON commands. Multiple clients can connect simultaneously (up to 4).

## Connection

```bash
nc <pico-ip> 4242
```

On connection, the server sends a hello banner:

```json
{"type":"hello","device":"EDGE-VIEW"}
```

## Commands

### Ping
```json
{"type":"ping"}
```
Response: `{"type":"pong"}`

### Weather Update
```json
{"type":"weather","temp":25,"cond":1}
```
- `temp`: Temperature in Celsius
- `cond`: Condition code (0=Sunny, 1=Cloudy, 2=Rain, 3=Storm, 4=Snow, 5=Fog)

### Text / AI Message
```json
{"type":"text","text":"Hello from AI"}
```
Displays the message on the AI status panel.

### Today's Verse
```json
{"type":"verse","text":"For God so loved the world...","ref":"John 3:16"}
```
Shown on the sidebar verse card. `text` is word-wrapped (max 320 chars),
`ref` is the reference line (max 64 chars).

### AI News Headline
```json
{"type":"news","text":"OpenAI announces..."}
```
Pushed into the bottom news banner ring buffer (12 headlines, 167 chars each).
Send repeatedly to build up the rotation.

### Time Sync
```json
{"type":"time","epoch":1753900000}
```
Sets the wall clock from a Unix epoch (UTC). Only needed when NTP is
unreachable — the device syncs itself against `pool.ntp.org` every 6 hours.
The displayed clock applies a +2h (SAST) offset.

### Game Mode
```json
{"type":"mode","name":"pacman"}
```
Switches the self-playing centre stage. Accepts `pacman`, `snake`, `bounce`, `off`.

> The field is `name`, not `mode`. The parser looks for `"name"`.

### Data (server-driven content)
```json
{"type":"data","key":"wx.temp","value":"21"}
```
Writes one value into the device's key/value store. Panels read straight out of
that store, so this is how the server paints the screen. 32 slots, keys up to
24 chars, values up to 192 chars; the least-recently-updated slot is recycled
when the table is full.

A value goes **stale after 5 minutes** and the panel falls back to whatever it
can work out locally. Keep the keys refreshed to stay in server-driven mode.

| Key | Used by | Example |
|---|---|---|
| `hdr.title`, `hdr.sub` | header | `IO-NITY EDGE-VIEW` |
| `clock`, `date` | header, clock | `14:32:05`, `FRI 31 JUL 2026` |
| `wx.temp`, `wx.cond`, `wx.place` | weather | `21`, `SUNNY`, `CENTURION` |
| `verse.text`, `verse.ref` | verse, rotate | |
| `marquee` | marquee | joined headlines |
| `text.title`, `text.body` | text | |
| `foot.left` | footer | `IONITY.CO.ZA` |

### Layout (interchangeable panels)
```json
{"type":"layout","slot":"stage","panel":"clock"}
```
Binds a named panel to a fixed screen slot, live, with no reflash.

**Slots:** `header`, `info_a`, `info_b`, `info_c`, `stage`, `side`, `marquee`,
`ticker`, `stats`, `footer`

**Panels:** `header`, `clock`, `network`, `feed`, `weather`, `game`, `verse`,
`qr`, `rotate`, `text`, `logo`, `marquee`, `news`, `stats`, `footer`, `blank`

Any panel may go in any slot — it is handed the slot's rectangle and draws to
fit. `blank` clears the slot.

### Query
```json
{"type":"query","what":"layout"}
```
The device replies on the same connection with its current bindings:
```json
{"type":"layout","slots":{"stage":"game",...},"panels":["header","clock",...]}
```

### Remote WiFi
```json
{"type":"wifi","ssid":"NewNetwork","pass":"secret"}
```
Saves the credentials to flash, replies `{"type":"wifi","saved":true}` and then
reboots onto the new network. **If the credentials are wrong you need physical
access to recover the device.**

```json
{"type":"wifi","reset":true}
```
Erases the saved credentials, replies `{"type":"wifi","cleared":true}` and
reboots. On the next boot the device falls back to the credentials compiled in
at build time; if there are none it starts its own provisioning access point
(`IO-nity-Setup`), so **you must be within WiFi range to recover it.**

`reset` is checked before `ssid`, so a command carrying both only clears.

### Sprite Control
```json
{"type":"sprite",0,100,200,2,2}
```
Move sprite at index 0 to position (100, 200) with velocity (2, 2).

### Reboot
```json
{"type":"reboot"}
```
Triggers a watchdog reboot of the Pico.

## Discovery Beacon (UDP 4243)

Once connected to WiFi, the device broadcasts to `255.255.255.255:4243`
every 5 seconds:

```
IONITY-EDGE <device-name> <ip> <stream-port>
```

Listen on UDP 4243 to auto-locate the device instead of hard-coding its IP.
The Studio app's Control tab does this automatically.

## Message Board (HTTP 80)

Any device on the same WiFi can post to the marquee without speaking the
stream protocol:

```bash
curl -X POST http://<pico-ip>/api/message \
     -d '{"author":"Jo","text":"Hello screen"}'
curl http://<pico-ip>/api/messages
```

The device also renders a QR code of `http://<pico-ip>/` on the sidebar so a
phone can simply scan it.

## Implementation

The stream server is implemented in `libionity/ionity_stream.c` using lwIP raw TCP API with `NO_SYS=1`.
TLS is **not** available on-device, so HTTPS feeds (verse, RSS news) are fetched by
the Studio app and pushed over port 4242. NTP and Open-Meteo are plain-protocol
and run directly on the Pico.

The key/value store lives in `libionity/ionity_data.c` and the slot/panel
registry in `libionity/ionity_scene.c`. The `edgeview` app registers its panels
against that registry, which is why the layout can be rearranged over the wire.

## Not supported: brightness

`IONITY_CMD_SET_BRIGHTNESS` exists in the parser but the `edgeview` app ignores
it. The panel drives its own backlight over DVI and the framebuffer is 3-bit
RGB — eight colours, no intermediate levels — so there is nothing to dim in
software. Use the monitor's own controls.

## Web console

The browser console at `docs/app/` does not speak this protocol directly:
browsers cannot open raw TCP, and an HTTPS page cannot reach a plain-HTTP
device. It talks to the Studio bridge on `http://127.0.0.1:8787`, which
translates REST calls into the commands above and relays every line back as
Server-Sent Events. See [Web-Console](Web-Console).