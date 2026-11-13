# Class demo: WebDashboard

A 3-minute live demo of the base station dashboard. It runs on fake rescuers,
so it works with no sensor nodes and no LoRa radio. Everything happens on a
fixed schedule, so you always know what's coming next.

## What you need

- The base station ESP32 (any ESP32 works) and a USB cable or power bank
- A laptop for the projector (a phone also works for the "join from your phone" moment)
- Backup: this repo on the laptop with Python 3, in case the board misbehaves

## Setup (do this before class)

1. **Flash the presentation build.** Open `WebDashboard.ino`, check that it says
   `#define DASH_DEMO 2` (the default), and upload it.
2. **Rehearse once, start to finish.** The schedule below starts when the board powers on.
3. In the classroom, power the board, join WiFi **LoRaRangers-Base**
   (password **rangers123**) and open **http://192.168.4.1**.
4. Press **F11** for full screen. Zoom the browser to about 110–125% so the back row can read it.
5. Right before you start: click **Clear history**, then press the board's
   **RST/EN** button. The clock restarts at 0:00 and the page picks up the
   restart by itself.

> The laptop drops off normal WiFi while it's on `LoRaRangers-Base`. Open any
> slides offline beforehand.

**Backup (no board):** `python tools/preview_server.py --present` shows the
same schedule at http://localhost:8000. Restart it, click Clear history, and go.

## Schedule (repeats every 2:30)

The header shows the board's uptime. Use it as your cue clock.

| Uptime | What happens on screen | What to point at |
|---|---|---|
| 0:00–0:05 | All three nodes report in and the cards turn **OK** | Cards, live clock, packet counter |
| 0:35–0:45 | Node 1 loses a packet. The next one shows **GAP** (amber) | Packet log: amber GAP row. Node 1 card: missed = 1, loss % |
| 0:55–1:00 | Node 2's packet arrives twice through a relay | Node 2 card: DUP = 1. The duplicate is *not* in the log or charts |
| 1:10 | Node 3 walks behind a ridge and goes quiet | "Last seen … s ago" keeps counting up |
| ~1:20 | Node 3 turns **LATE** (amber) | Status badge |
| ~1:35 | Node 3 turns **LOST**: a red banner with its last position, and the card pulses | Banner: "last seen 17:09:04 at 37.2025, -113.6382". Click **Copy** on its position |
| ~1:58 | Node 3 comes back: **OK**, plus a GAP of ~9 missed packets | Log: GAP row. Node 3: missed = 9, banner gone |

## Talk track (about 3 minutes)

**Opening (0:00).** "This is our base station. The ESP32 that receives the LoRa
packets also runs its own WiFi network and web server. A search team lead joins
that network with any phone or laptop and sees every rescuer live. It needs no
internet, which is the point: there isn't any in Snow Canyon."

**The cards (0:10).** "Each rescuer has a card with temperature and humidity
from the DHT11, battery, signal strength, GPS position, and how long ago we
last heard from them. Every packet is time-stamped when it *arrives*, because
the nodes don't share a clock. They only know milliseconds since they powered on."

**Map and charts (0:20).** Hover a dot on the map. "The trail fades with age.
The map is drawn by hand in SVG, with no map tiles, so it works offline. The
scale bar is in real meters." Switch the charts between 10 min / 30 min / All.

**Lost packet (0:40).** "Node 1 just missed a packet. Every packet carries a
sequence number, so when 9 arrives after 7 we know 8 never made it. That's the
amber GAP row. The card counts it in packet loss."

**Duplicate (1:00).** "Our network relays packets from node to node, so the
base can hear the same packet twice. Node 2's was just counted as a duplicate
and ignored, so it doesn't distort the charts."

**The key moment (1:10–1:40).** "Now watch Node 3. It walks out of range…
'last seen' keeps counting… LATE… and now **LOST**. In a real search this is
the alert that matters: it tells you *where* we last heard from that person, so
a team knows where to start." Click **Copy** on the coordinates.

**Recovery (1:58).** "Node 3 is back. The dashboard shows how many packets we
missed while it was out of range."

**Wrap-up.** Refresh the page: "The history survives a refresh. It's stored in
the browser." Click **Download CSV**: "And it exports everything for the report."

> Say once that this is the presentation build: nodes send every 5 s instead
> of 30 s, and LOST comes after 25 s instead of 150 s. The badge in the header
> says so too.

## Likely questions

| Question | Answer |
|---|---|
| Why does the ESP32 host the page itself? | No internet in the field. One board, no laptop software needed, and several people can watch at once. |
| How do you detect a missed packet? | Sequence numbers. A jump forward is a GAP, the same number again is a duplicate, a number slightly behind is an old (late) packet. The number wraps from 65535 back to 0 correctly. |
| What if a node reboots? | Its clock goes back to near zero. The dashboard spots that and starts tracking it again instead of counting thousands of "missed" packets. |
| Why 60 s / 150 s in real use? | Nodes send every 30 s. LATE means two missed sends, LOST means five. |
| How does the page get data? | It asks `/data?since=<last id>` every second and only gets packets it hasn't seen yet. |
| How is it tested? | 212 automated checks run on a PC with fake ESP32 headers: CSV parsing, gaps, duplicates, wrap-around, reboots, timing, JSON validity and buffer overflows. It also compiles for the real board. |
| Does it use real LoRa data? | Our decoder (`PacketCodec`) plugs in with one line, `dashboard.update(toDashboardReading(pkt, rssi), millis())`. The demo uses fake nodes so it runs the same every time. |

## If something goes wrong

| Problem | Fix |
|---|---|
| Page won't load | Check the laptop is on `LoRaRangers-Base`, and use `http://` (not https). |
| "Base station disconnected" | The laptop jumped back to school WiFi. Rejoin `LoRaRangers-Base`. |
| Old data on screen | Click Clear history, then press RST on the board. |
| The board won't work at all | Switch to the backup: `python tools/preview_server.py --present`. |
