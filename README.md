# Claude Credit Monitor

Shows your **Claude Code usage** on a **Waveshare ESP32-S3-RLCD-4.2** (400×300 reflective monochrome LCD). Three boxed cards show the current 5-hour session, today, and this week against limits you choose, and a pixel-art creature on the left reacts to how hard you're pushing.

Under the hood there are two data sources:

- **v4 (main view):** a small Python bridge on your laptop reads Claude Code's local logs with [ccusage](https://github.com/ryoppippi/ccusage) every 5 minutes and pushes the totals to a Cloudflare Worker.
- **v1–v3 (fallback):** the same Worker can proxy Anthropic API calls and log each one to D1. Its month-to-date total still appears as a one-line "Month $X/$Y" strip on the v4 screen, and the full v3 layout is one button press away.

## How it works

```
 (laptop)  Claude Code writes JSONL
                 ↓
           ccusage --json  ←  bridge/bridge.py   every 5 min (Task Scheduler)
                 ↓  POST /ingest-cc  (X-Ingest-Auth)
 (cloud)   Cloudflare Worker  →  D1: cc_usage (single row)
                 ↓
           GET /stats  →  "cc" block (hidden if older than 15 min)
                 ↓
 (device)  ESP32 polls every 30 s  →  cards + mascot

 (optional) Your code → Worker /v1/messages → api.anthropic.com
                            ↓ logs each call to D1: usage
                        /stats "month" block → "Month $X/$Y" strip
```

The bridge only needs a shared `INGEST_SECRET`; it never sees or uses an Anthropic key.

## What you see on the display

**v4 main view (default):**

```
┌───────────────────────────────────────────────┐
│ ┌──────────┐  ┌──────────────────────────────┐│
│ │          │  │ CURRENT                  50% ││  5-hour session block
│ │  ▄████▄  │  │ [██████████░░░░░░░░░░░░░░░]  ││
│ │  █ ██ █  │  │ Resets in 1h 22m             ││
│ │  ██▄▄██  │  └──────────────────────────────┘│
│ │  ▀█  █▀  │  ┌──────────────────────────────┐│
│ │          │  │ TODAY                    11% ││  calendar day (Bangkok)
│ │   idle   │  │ [██░░░░░░░░░░░░░░░░░░░░░░░]  ││
│ │          │  │ Resets in 8h 15m             ││
│ │ mascot   │  └──────────────────────────────┘│
│ │ column   │  ┌──────────────────────────────┐│
│ │          │  │ WEEKLY                   25% ││  plan week, resets Wed 14:00 (Bangkok)
│ │          │  │ [█████░░░░░░░░░░░░░░░░░░░░]  ││
│ │          │  │ Resets in 4d 8h              ││
│ └──────────┘  └──────────────────────────────┘│
│               Month $16.50/$500               │  fallback: proxy month-to-date
└───────────────────────────────────────────────┘
```

- **Percent** = cost ÷ the limit you set in `bridge/.env`. Shown as `<1%`, a whole number (rounded down), or `>100%`.
- **Bars** fill proportionally. At ≥ 80% a second outline appears around the bar; at ≥ 100% the bar is solid with a white inner outline.
- **Offline:** if the bridge hasn't reported in 15 minutes, each card shows `-`, an empty bar and `Resets in -`, and the mascot goes to sleep.

**Mascot states** (picked from the highest card):

| State | When | Animation |
|---|---|---|
| sleeping | bridge offline, or every card < 5% | lying on its side, eyes shut, `zZ` |
| idle | every card < 25% | blinks every 4 s |
| active | any card 25–60% | small hop every 3 s |
| busy | any card 60–80% | bigger hop every 1 s |
| critical | any card ≥ 80% | panel inverted, jumps every 0.5 s |
| worried | any card ≥ 100% | head-tilt wobble every 0.5 s |

**KEY button** toggles to the **v3 view** (monthly budget bar, burn rate, rate-limit bars, 7-day sparkline, all from the proxy's own usage log) and back. **BOOT** forces an immediate refresh.

---

## Prerequisites

| Tool | Version |
|---|---|
| Node.js | 18+ (for Wrangler and ccusage) |
| Wrangler CLI | `npm i -g wrangler` (or `npx wrangler` inside `proxy/`) |
| ccusage | `npm i -g ccusage` (v20+; for the bridge) |
| Python | 3.10+ on Windows (for the bridge) |
| PlatformIO Core | `pip install platformio` or [IDE plugin](https://platformio.org/install) |
| A Cloudflare account | Free tier works |
| Anthropic API key | `sk-ant-api03-…`. Only needed if you route API calls through the proxy |

---

## Part 1 — Cloudflare Worker

### 1. Install dependencies

```bash
cd proxy
npm install
```

### 2. Create the D1 database

```bash
wrangler d1 create claude-credit-monitor
```

Copy the `database_id` from the output and paste it into `wrangler.toml`:

```toml
[[d1_databases]]
binding = "DB"
database_name = "claude-credit-monitor"
database_id   = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"   # ← paste here
```

### 3. Apply the schema

```bash
wrangler d1 execute claude-credit-monitor --file=schema.sql    --remote   # usage log
wrangler d1 execute claude-credit-monitor --file=schema_v2.sql --remote   # ratelimits
wrangler d1 execute claude-credit-monitor --file=schema_v3.sql --remote   # org_spend
wrangler d1 execute claude-credit-monitor --file=schema_v4.sql --remote   # cc_usage (v4 bridge)
```

Drop `--remote` to target the local dev database. Every file after `schema.sql` is additive and safe to re-run (single-row tables use `INSERT OR IGNORE`).

**Upgrading an existing deployment to v4:** apply only `schema_v4.sql`, set `INGEST_SECRET` (next step), and redeploy.

Check that the v4 row exists:

```bash
wrangler d1 execute claude-credit-monitor --remote --command "SELECT id, updated_at FROM cc_usage"
# → one row: id 1, updated_at 0 (never ingested yet)
```

### 4. Set secrets

```bash
wrangler secret put PROXY_SHARED_SECRET   # firmware + API clients → /stats and the proxy
wrangler secret put INGEST_SECRET         # v4 bridge → /ingest-cc
wrangler secret put ANTHROPIC_API_KEY     # only if you proxy API calls
```

Generate each shared secret as 32 random hex characters, for example:

```bash
python -c "import secrets; print(secrets.token_hex(16))"
```

Keep the `INGEST_SECRET` value: it goes into `bridge/.env` in Part 3.

The monthly cap on the "Month" strip and the v3 view comes from `MONTHLY_LIMIT_USD` in `wrangler.toml` (default `"500"`). Month-to-date spend is summed from the proxy's own `usage` table (calendar month, Bangkok time).

For local dev: `cp .dev.vars.example .dev.vars` and fill in the values.

### 5. Deploy

```bash
npm run deploy
```

Wrangler prints the Worker URL (`https://claude-credit-proxy.<your-subdomain>.workers.dev`). The firmware, the bridge and any API clients all need it.

### 6. Smoke-test

```bash
URL=https://claude-credit-proxy.<subdomain>.workers.dev

# v4 ingest: wrong/missing secret → 401, bad body → 400, good body → {"ok":true}
curl -X POST $URL/ingest-cc -H "X-Ingest-Auth: <INGEST_SECRET>" -H "Content-Type: application/json" \
  -d '{"session":{"cost":9,"pct":0.5,"reset_at":0,"tokens":1},
       "today":{"cost":2.75,"pct":0.11,"reset_at":0,"tokens":1},
       "week":{"cost":30,"pct":0.25,"reset_at":0,"tokens":1}}'

# /stats now includes "cc" (for 15 minutes after the last ingest) and "month"
curl $URL/stats -H "X-Proxy-Auth: <PROXY_SHARED_SECRET>"

# Optional: a proxied API call, then check it was logged
curl $URL/v1/messages -H "X-Proxy-Auth: <PROXY_SHARED_SECRET>" -H "Content-Type: application/json" \
  -d '{"model":"claude-haiku-4-5-20251001","max_tokens":32,"messages":[{"role":"user","content":"ping"}]}'
wrangler d1 execute claude-credit-monitor --remote --command "SELECT * FROM usage ORDER BY id DESC LIMIT 3"
```

**API reference**

| Endpoint | Auth header | Purpose |
|---|---|---|
| `POST /ingest-cc` | `X-Ingest-Auth: INGEST_SECRET` | Bridge upload; body `{session, today, week}`, each `{cost, pct, reset_at, tokens}` |
| `GET /stats` | `X-Proxy-Auth: PROXY_SHARED_SECRET` | Everything the firmware shows; `cc` appears only while bridge data is < 15 min old |
| anything else | `X-Proxy-Auth` or `x-api-key` | Proxied to `api.anthropic.com` and logged |

> While `cc` is fresh it replaces the older top-level `cc` number (24 h cache-creation tokens) in `/stats`. Only the v3 view's cache-creation figure uses that number, and it reads 0 while the bridge is live.

---

## Part 2 — ESP32 firmware

### Hardware

**Waveshare ESP32-S3-RLCD-4.2**
- MCU: ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB OPI PSRAM)
- Display: 4.2″ reflective LCD, ST7305 driver, 400×300 monochrome, SPI
- Buttons: BOOT (GPIO 0) = refresh now, KEY (GPIO 18) = toggle v4 / v3 view

| Signal | GPIO |
|---|---|
| CLK | 11 |
| MOSI | 12 |
| CS | 40 |
| DC | 5 |
| RST | 41 |

### 1. Install PlatformIO

```bash
pip install platformio
# or install the VS Code extension from platformio.org
```

### 2. Create `config.h`

```bash
cd firmware
cp src/config.h.example src/config.h
```

Edit `src/config.h`:

```cpp
#define WIFI_SSID        "your-network-name"
#define WIFI_PASSWORD    "your-password"
#define PROXY_URL        "https://claude-credit-proxy.<subdomain>.workers.dev/stats"
#define PROXY_SECRET     "<same 32-char hex as PROXY_SHARED_SECRET>"
#define POLL_INTERVAL_MS 30000    // 30 seconds (v4)
```

> `config.h` is git-ignored. Never commit it.

### 3. Build and flash

```bash
pio run --target upload
```

On first run PlatformIO downloads the ESP32 platform and ArduinoJson automatically.

### 4. Monitor serial output

```bash
pio device monitor --baud 115200
```

Expected boot sequence:

```
Claude Credit Monitor
WiFi → your-network-name
WiFi 192.168.x.x
NTP sync........ OK
stats: in=12345 out=6789 usd=0.1900
  day: in=8000 out=4000 usd=0.1100
  week: in=50000 out=25000 usd=0.7200
  month: usd=16.50 lim=500.00 proj=18.33 (day 28/30)
  cc: session $3.28 18.2%  today $3.28 13.1%  week $22.69 18.9%
```

`cc: (absent — bridge offline)` means the Worker has no bridge data newer than 15 minutes. An `rl:` line appears once an API call has gone through the proxy.

**Mascot test keys:** type `0`–`5` in the serial monitor to force a mascot state (sleeping, idle, active, busy, critical, worried), and `a` to return to automatic.

### 5. Ongoing behaviour

| Event | Behaviour |
|---|---|
| Every 30 s | Fetches `/stats`; the cards are redrawn only if what they show changed (new data or a countdown reaching a new minute) |
| Every 100 ms | Mascot animation tick; only the mascot column is pushed, and only when the frame changed |
| Bridge silent > 15 min | Cards go offline (`-`), mascot sleeps |
| Fetch fails | Keeps last-good data on screen (cards still go offline once it's > 15 min old) |
| WiFi drops | Reconnects |

The mascot and card columns are pushed to the panel separately (`DisplayST7305::refreshColumns`). If partial updates ever misbehave on your panel, set `#define V4_PARTIAL_REFRESH 0` in `src/layout_v4.cpp` to push full frames instead.

---

## Part 3 — v4 laptop bridge (Claude Code usage)

`bridge/bridge.py` reads your local Claude Code usage via ccusage, works out the three windows, and POSTs them to the Worker's `/ingest-cc` endpoint.

| Card | Source | Resets |
|---|---|---|
| CURRENT | `ccusage claude blocks --active`: the active 5-hour block | when the block ends (`0`/unknown if no block is running) |
| TODAY | `ccusage claude daily`: today's row | midnight in `TIMEZONE` |
| WEEKLY | `ccusage claude daily` from the last Wednesday-14:00 reset's date to today, summed (the whole Wednesday counts, since `--since` is per day) | next Wednesday 14:00 in `TIMEZONE` (`WEEK_RESET_*` in `bridge.py`) |

It uses `ccusage claude …` on purpose: plain `ccusage daily` (v20+) also counts Codex and other coding agents.

### 1. Install

```powershell
npm install -g ccusage
python -m pip install --user -r bridge\requirements.txt   # requests + tzdata
```

`tzdata` is needed because Python on Windows ships without IANA time zones (`Asia/Bangkok` would otherwise fail).

### 2. Configure `bridge/.env`

Create `bridge/.env` (git-ignored) next to the script:

```ini
WORKER_URL=https://claude-credit-proxy.<subdomain>.workers.dev
INGEST_SECRET=<same value you gave wrangler secret put INGEST_SECRET>
PLAN_SESSION_LIMIT_USD=18.00     # approx per-5h soft limit for your plan
PLAN_DAILY_LIMIT_USD=25.00       # your chosen daily cap
PLAN_WEEKLY_LIMIT_USD=120.00     # your chosen weekly cap
TIMEZONE=Asia/Bangkok
```

The limits are only used to turn dollars into percentages. Tune them to your plan and habits; nothing is auto-detected.

If you're setting up from scratch, generate the secret once and use it in both places:

```powershell
$s = python -c "import secrets; print(secrets.token_hex(16))"
Push-Location proxy; $s | npx wrangler secret put INGEST_SECRET; Pop-Location
$s   # paste this into INGEST_SECRET= in bridge/.env (secret put takes effect without a redeploy)
```

### 3. Test by hand

```powershell
python bridge\bridge.py --dry-run   # build the payload and print it, nothing sent
python bridge\bridge.py             # build and send; one line appended to bridge\bridge.log
Get-Content bridge\bridge.log -Tail 1
# 2026-11-28 13:30:41,418 INFO sent  session $1.49 (8.3%)  today $1.49 (6.0%)  week $20.89 (17.4%)
```

Then confirm the Worker stored it:

```powershell
cd proxy
npx wrangler d1 execute claude-credit-monitor --remote --command "SELECT updated_at, session_pct, today_pct, week_pct FROM cc_usage"
```

`bridge.log` rotates at 10 MB (two old files kept). A failed run logs one `ERROR` line with the reason and exits 1; the next scheduled run simply tries again.

### 4. Schedule it every 5 minutes (Windows Task Scheduler)

The repo includes a script that registers the task:

```powershell
powershell -ExecutionPolicy Bypass -File bridge\install-task.ps1
```

It creates a task named **ClaudeUsageBridge** with these settings:

| Setting | Value |
|---|---|
| Trigger | Every 5 minutes, indefinitely (first run ~1 min after install) |
| Action | `pythonw.exe "<project>\bridge\bridge.py"` (pythonw = no console flash) |
| Start in | the project folder |
| Runs as | you, only while you're logged on (ccusage reads your profile's Claude Code logs) |
| On battery | allowed, and not stopped when unplugged (Windows' default would skip runs on a laptop) |
| Missed runs | run as soon as possible after a missed start (e.g. after sleep) |
| Overlap / time limit | a new run is skipped if one is still going; each run is killed after 2 min |

`install-task.ps1` uses the `pythonw.exe` next to the first real `python.exe` on PATH, skipping the Microsoft Store stub. Pass `-PythonW <path>` to choose a different one. Re-running the script replaces the task.

Don't schedule it more often than every 5 minutes: Claude Code's logs aren't written in real time, so faster runs add load without fresher numbers.

**"Run whether user is logged on or not"** (optional, keeps the bridge running behind the lock screen): open Task Scheduler → *ClaudeUsageBridge* → Properties → General, select that option, and enter your Windows password. The lock screen alone doesn't stop the default setup, because you're still logged on; only signing out does.

### 5. Check that it's running

```powershell
Get-ScheduledTaskInfo ClaudeUsageBridge | Format-List LastRunTime, LastTaskResult, NextRunTime
Get-Content bridge\bridge.log -Tail 5
```

`LastTaskResult` is `0` after a good run and `1` when the run failed; the reason is the matching `ERROR` line in `bridge.log`. Within one 30-second poll after a good run, the device's cards should match the last `sent` line.

**Pause / resume / remove**

```powershell
Disable-ScheduledTask ClaudeUsageBridge      # cards go offline ~15 min later (handy for testing)
Enable-ScheduledTask  ClaudeUsageBridge
Unregister-ScheduledTask -TaskName ClaudeUsageBridge -Confirm:$false
```

### Troubleshooting

| Symptom | Likely cause |
|---|---|
| `ERROR failed: ccusage not found on PATH` | `npm install -g ccusage`, then make sure npm's global folder is on your user PATH |
| `ERROR ... -> 401: Unauthorized` | `INGEST_SECRET` in `bridge/.env` doesn't match the Worker secret |
| `ZoneInfoNotFoundError: Asia/Bangkok` | `pip install --user tzdata` |
| Log is fine but the device shows `-` | Device can't reach `/stats`, or its clock isn't synced (check serial output) |
| CURRENT shows `Resets in -` | No active 5-hour block right now (you haven't used Claude Code recently) |

---

## Migrating your client code (optional proxy)

Replace `api.anthropic.com` with the Worker URL and add `X-Proxy-Auth`. Your real Anthropic key stays in the Worker — clients never see it.

### Python SDK

```python
from anthropic import Anthropic

client = Anthropic(
    api_key="dummy",          # ignored — proxy injects the real one
    base_url="https://claude-credit-proxy.<subdomain>.workers.dev",
    default_headers={"X-Proxy-Auth": "<PROXY_SHARED_SECRET>"},
)

message = client.messages.create(
    model="claude-sonnet-4-6",
    max_tokens=1024,
    messages=[{"role": "user", "content": "Hello"}],
)
```

### TypeScript / Node SDK

```typescript
import Anthropic from "@anthropic-ai/sdk";

const client = new Anthropic({
  apiKey: "dummy",
  baseURL: "https://claude-credit-proxy.<subdomain>.workers.dev",
  defaultHeaders: { "X-Proxy-Auth": "<PROXY_SHARED_SECRET>" },
});

const msg = await client.messages.create({
  model: "claude-sonnet-4-6",
  max_tokens: 1024,
  messages: [{ role: "user", content: "Hello" }],
});
```

### curl

```bash
curl https://claude-credit-proxy.<subdomain>.workers.dev/v1/messages \
  -H "X-Proxy-Auth: <PROXY_SHARED_SECRET>" \
  -H "Content-Type: application/json" \
  -d '{
    "model": "claude-sonnet-4-6",
    "max_tokens": 1024,
    "messages": [{"role": "user", "content": "Hello"}]
  }'
```

---

## Updating pricing

The proxy's pricing table lives in `proxy/src/pricing.ts`. When Anthropic releases new models or changes rates, update the file and redeploy:

```bash
# Edit pricing.ts, then:
cd proxy && npm run deploy
```

Unknown models are logged as `cost_usd = 0` with a `console.warn`. Check Worker logs in the Cloudflare dashboard if you see unexpected zeroes. (The v4 cards use ccusage's own pricing, not this table.)

---

## Repo layout

```
claude-credit-monitor/
├── README.md
├── bridge/                     # v4 laptop bridge
│   ├── bridge.py               # ccusage → payload → POST /ingest-cc, logs to bridge.log
│   ├── install-task.ps1        # registers the 5-min ClaudeUsageBridge scheduled task
│   ├── requirements.txt        # requests, tzdata
│   └── .env                    # (git-ignored) WORKER_URL, INGEST_SECRET, plan limits
├── proxy/
│   ├── src/
│   │   ├── worker.ts           # Routing, auth, proxy pass-through, D1 usage logging
│   │   ├── ingest.ts           # (v4) POST /ingest-cc + the /stats "cc" block
│   │   ├── stats.ts            # /stats endpoint (+ month block from the usage log)
│   │   ├── pricing.ts          # Model → USD/1M token rates
│   │   └── types.ts            # Shared TypeScript interfaces
│   ├── schema.sql              # usage table
│   ├── schema_v2.sql           # ratelimits table (additive)
│   ├── schema_v3.sql           # org_spend table (additive)
│   ├── schema_v4.sql           # cc_usage table (additive)
│   ├── wrangler.toml           # Worker name, D1 binding, MONTHLY_LIMIT_USD
│   ├── package.json
│   ├── tsconfig.json
│   └── .dev.vars.example
└── firmware/
    ├── platformio.ini          # ESP32-S3, 16 MB flash, OPI PSRAM
    └── src/
        ├── main.cpp              # WiFi, NTP, 30 s poll loop, buttons, view switching
        ├── display_st7305.h/.cpp # ST7305 driver, PSRAM framebuffer, partial column refresh
        ├── layout_v4.h/.cpp      # v4 view: cards, Month strip, mascot column, change detection
        ├── mascot.h/.cpp         # Pixel creature: states, frame timing, poses
        ├── layout.h/.cpp         # v3 view (KEY) + StatsData
        ├── sparkline.h/.cpp      # 7-bar chart (v3 view)
        └── config.h.example      # Credentials template
```

---

## Limitations

- **v4 numbers are estimates.** ccusage prices Claude Code's local logs at API rates; plan limits in `bridge/.env` are your own settings, not values read from Anthropic. The weekly window is computed from ccusage, not from Anthropic's own weekly limit.
- **The bridge only sees the machine it runs on**, and only while you're logged on. Claude Code use on another computer isn't counted.
- **Up to ~5.5 minutes of lag** for the cards (5-min bridge cycle + 30-s device poll).
- The **optional proxy** captures only API calls routed through it. claude.ai chat and Claude Code don't go through it, which is why v4 uses the bridge.
- No HTTPS certificate pinning on the ESP32 (`setInsecure()`). Acceptable for a personal hobby build on a Cloudflare-signed URL.
- The 5×8 bitmap font is ASCII-only and has no true descenders (e.g. `sleeping` looks slightly off, and `-` stands in for an em dash).
- **Monthly cap** comes from `MONTHLY_LIMIT_USD`, not an API (Anthropic's Spend Limits API is Enterprise-only).
