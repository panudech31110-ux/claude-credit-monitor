# Claude Credit Monitor

Track Anthropic API usage in real time on a **Waveshare ESP32-S3-RLCD-4.2** (400×300 reflective monochrome LCD). Core per-call usage tracking needs only a standard API key; adding an **Admin API key** (optional, v3) unlocks real month-to-date spend, a budget % bar, and an end-of-month projection.

## How it works

```
Your code  →  Cloudflare Worker (/v1/messages)  →  api.anthropic.com
                        ↓  logs each call to D1
                    D1 database
                        ↑
              /stats  ← ESP32 polls every 2 min

   (v3) Cron every 15 min  →  Admin API /v1/organizations/cost_report
                        ↓  upserts org_spend singleton
                    D1 database
```

A Cloudflare Worker sits in front of `api.anthropic.com`. It proxies every request, strips your shared secret, injects the real Anthropic key, and writes one row to D1 per API call. A separate `/stats` endpoint aggregates 24-hour totals and a 7-day sparkline for the firmware to display.

**v3** adds a scheduled (cron) handler that calls Anthropic's **Admin API** every 15 minutes for the organization's real month-to-date spend, stores it in the `org_spend` singleton table, and exposes it as a `month` block on `/stats`. The firmware renders this as a monthly-budget bar with burn-rate projection. If no Admin key is configured (the `month` block never appears), the firmware falls back to the v2 layout automatically.

## What you see on the display

**v3 layout — when an Admin API key is configured (`/stats` includes `month`):**

```
┌──────────────────────────────────────────┐
│ ▌▌▌ CLAUDE API MONITOR      Day 27 / 30  │  header (WiFi glyph + date, Bangkok)
├──────────────────────────────────────────┤
│  MONTHLY BUDGET                          │  "!" prefix when ≥ 80% of cap
│  $0.75 / $500                       <1%  │  month-to-date / cap, % right-aligned
│  [███░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]  │  fills proportionally; pinned at ≥100%
│  Projected EOM: $0.83  (<1% of cap)      │  burn-rate projection to month end
├──────────────────────────────────────────┤
│  BURN RATE                               │  CREDIT BALANCE shown too if available
│  $0.03 / day                             │  (no credit endpoint → full-width burn)
├──────────────────────────────────────────┤
│  RPM  ████████░░  42/60                  │  rate-limit headroom (RPM + ITPM only;
│  ITPM █████████░  180K/200K             │  OTPM dropped for space)
├──────────────────────────────────────────┤
│  7-day spend ($)                         │  7-day sparkline
│  ▂ ▃ ▁ ▅ ▂ ▇ ▄                          │  today = rightmost, outlined
├──────────────────────────────────────────┤
│  User btn: next view    Boot: refresh    │  footer
└──────────────────────────────────────────┘
```

**v2 fallback layout — when no Admin key is set (`month` block absent):**

```
┌──────────────────────────────────────────┐
│ ! Admin API not configured      21:05    │  warning replaces the title
├──────────────────────────────────────────┤
│  TODAY              │  WEEK              │  calendar day (Bangkok UTC+7)
│  $0.19              │  $1.34             │  and 7-day rolling cost
│  12K tok            │  85K tok           │  in+out shorthand (K / M)
├──────────────────────────────────────────┤
│  24h  in:  12,345    cr:  5,000          │  24-hour rolling token counts
│       out:  6,789    cc:    800          │
├──────────────────────────────────────────┤
│  RPM/ITPM/OTPM bars + 7-day sparkline    │  identical to v2
└──────────────────────────────────────────┘
```

The rate-limit section is hidden (and the sparkline expands to fill the space) until the first API call flows through the proxy and populates the `ratelimits` table. The firmware switches between the two layouts automatically based on whether `/stats` returns a `month` block.

---

## Prerequisites

| Tool | Version |
|---|---|
| Node.js | 18+ |
| Wrangler CLI | `npm i -g wrangler` |
| PlatformIO Core | `pip install platformio` or [IDE plugin](https://platformio.org/install) |
| A Cloudflare account | Free tier works |
| Anthropic API key | `sk-ant-api03-…` |
| Admin API key (optional, v3) | `sk-ant-admin-…` — for real monthly spend |

---

## Part 1 — Cloudflare Worker proxy

### 1. Install dependencies

```bash
cd proxy
npm install
```

### 2. Create the D1 database

```bash
wrangler d1 create claude-credit-db
```

Copy the `database_id` from the output and paste it into `wrangler.toml`:

```toml
[[d1_databases]]
binding = "DB"
database_name = "claude-credit-db"
database_id   = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"   # ← paste here
```

### 3. Apply the schema

```bash
# Against the remote (production) database
wrangler d1 execute claude-credit-db --file=schema.sql --remote
wrangler d1 execute claude-credit-db --file=schema_v2.sql --remote
wrangler d1 execute claude-credit-db --file=schema_v3.sql --remote

# Or against the local dev database
wrangler d1 execute claude-credit-db --file=schema.sql
wrangler d1 execute claude-credit-db --file=schema_v2.sql
wrangler d1 execute claude-credit-db --file=schema_v3.sql
```

`schema_v2.sql` adds the `ratelimits` singleton table used for rate-limit headroom display. `schema_v3.sql` adds the `org_spend` singleton table used for monthly spend. Both are safe to run against an existing database — they use `INSERT OR IGNORE` for the singleton row.

#### Migrating an existing deployment

If you already have v1/v2 deployed, apply only the newer schema(s) (the `usage` table is unchanged):

```bash
wrangler d1 execute claude-credit-db --file=schema_v2.sql --remote   # if upgrading from v1
wrangler d1 execute claude-credit-db --file=schema_v3.sql --remote   # for v3 monthly spend
```

Then redeploy the worker:

```bash
cd proxy && npm run deploy
```

### 4. Set secrets

```bash
wrangler secret put ANTHROPIC_API_KEY
# paste: sk-ant-api03-...

wrangler secret put PROXY_SHARED_SECRET
# paste: <32-char random hex, e.g. from `openssl rand -hex 16`>
```

#### (v3, optional) Admin API key for monthly spend

To show real month-to-date spend, create an **Admin** key and store it as a secret:

1. In the [Anthropic Console](https://console.anthropic.com), go to **Settings → Admin Keys → Create Key** and copy the `sk-ant-admin-…` value.
2. Store it (never paste it into code, a commit, or `/stats`):

   ```bash
   wrangler secret put ADMIN_API_KEY
   # paste: sk-ant-admin-...
   ```

The cron trigger (`[triggers] crons = ["*/15 * * * *"]` in `wrangler.toml`) and the `MONTHLY_LIMIT_USD` config var (default `"500"`) are already set. The monthly cap comes from `MONTHLY_LIMIT_USD` because Anthropic's **Spend Limits API is Enterprise-only** and not available to Console organizations — set this to your tier cap (shown on the Console *Rate limits* page). Skip this step entirely and the firmware falls back to the v2 layout with an "Admin API not configured" header.

For local dev, copy the example file instead:

```bash
cp .dev.vars.example .dev.vars
# edit .dev.vars and fill both values
```

### 5. Deploy

```bash
npm run deploy
```

Wrangler prints the Worker URL:

```
https://claude-credit-proxy.<your-subdomain>.workers.dev
```

Keep this URL — you need it for the firmware and for migrating your client code.

### 6. Smoke-test

```bash
# Non-streaming call
curl https://claude-credit-proxy.<subdomain>.workers.dev/v1/messages \
  -H "X-Proxy-Auth: <PROXY_SHARED_SECRET>" \
  -H "Content-Type: application/json" \
  -d '{
    "model": "claude-haiku-4-5-20251001",
    "max_tokens": 32,
    "messages": [{"role":"user","content":"ping"}]
  }'

# Streaming call
curl https://claude-credit-proxy.<subdomain>.workers.dev/v1/messages \
  -H "X-Proxy-Auth: <PROXY_SHARED_SECRET>" \
  -H "Content-Type: application/json" \
  --no-buffer \
  -d '{
    "model": "claude-haiku-4-5-20251001",
    "max_tokens": 32,
    "stream": true,
    "messages": [{"role":"user","content":"count to 3"}]
  }'

# Check a row was logged
wrangler d1 execute claude-credit-db \
  --command "SELECT * FROM usage ORDER BY id DESC LIMIT 3" --remote

# Check /stats
curl https://claude-credit-proxy.<subdomain>.workers.dev/stats \
  -H "X-Proxy-Auth: <PROXY_SHARED_SECRET>"
```

---

## Part 2 — ESP32 firmware

### Hardware

**Waveshare ESP32-S3-RLCD-4.2**
- MCU: ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB OPI PSRAM)
- Display: 4.2″ reflective LCD, ST7305 driver, 400×300 monochrome, SPI

The display pins are fixed on-board:

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
#define POLL_INTERVAL_MS 120000   // 2 minutes
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
  rl: rpm=42/60 itpm=180000/200000 otpm=75000/80000
  month: usd=0.75 lim=500.00 proj=0.83 (day 27/30)
```

The `rl:` line appears only after the first API call flows through the proxy. The `month:` line appears once the v3 cron has populated `org_spend` (otherwise it logs `month: (absent — fallback to v2 layout)`).

The display shows "connecting…" → "syncing time…" → "fetching…" → full layout.

### 5. Ongoing behaviour

| Event | Behaviour |
|---|---|
| Every 2 min | Fetches `/stats`, redraws |
| WiFi drops | Reconnects; shows `!` next to time while offline |
| Fetch fails | Keeps last-good data on screen; shows `!` |
| No data yet | All-zero layout with `!` |

---

## Migrating your client code

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

The pricing table lives in `proxy/src/pricing.ts`. When Anthropic releases new models or changes rates, update the file and redeploy:

```bash
# Edit pricing.ts, then:
cd proxy && npm run deploy
```

Unknown models are logged as `cost_usd = 0` with a `console.warn` — check Worker logs in the Cloudflare dashboard if you see unexpected zeroes.

---

## Repo layout

```
claude-credit-monitor/
├── README.md
├── proxy/
│   ├── src/
│   │   ├── worker.ts       # Auth, proxy pass-through, D1 logging, cron entry
│   │   ├── scheduled.ts    # (v3) 15-min cron: Admin API → org_spend
│   │   ├── pricing.ts      # Model → USD/1M token rates
│   │   ├── stats.ts        # /stats endpoint (+ v3 month block)
│   │   └── types.ts        # Shared TypeScript interfaces
│   ├── schema.sql          # D1 usage table
│   ├── schema_v2.sql       # D1 ratelimits table (additive — safe to run on existing DB)
│   ├── schema_v3.sql       # D1 org_spend table (additive — safe to run on existing DB)
│   ├── wrangler.toml       # Worker name, D1 binding, cron trigger, MONTHLY_LIMIT_USD
│   ├── package.json
│   ├── tsconfig.json
│   └── .dev.vars.example
└── firmware/
    ├── platformio.ini      # ESP32-S3, 16 MB flash, OPI PSRAM
    └── src/
        ├── main.cpp              # WiFi, NTP, poll loop
        ├── display_st7305.h/.cpp # ST7305 driver, PSRAM framebuffer
        ├── sparkline.h/.cpp      # 7-bar chart
        ├── layout.h/.cpp         # Screen layout renderer
        └── config.h.example      # Credentials template
```

---

## Limitations

- Usage from **claude.ai** chat and **Claude Code CLI** is not captured — those calls bypass the proxy by design.
- No HTTPS certificate pinning on the ESP32 (`setInsecure()`). Acceptable for a personal hobby build on a Cloudflare-signed URL.
- On-board sensors (SHTC3 temp/humidity, PCF85063 RTC, ES8311 audio) are not used.
- Pricing table requires a manual update when Anthropic adds models.
- Rate-limit bars remain hidden until at least one API call is proxied (Anthropic only sends rate-limit headers in API responses, not proactively).
- **(v3) Monthly cap** is read from the `MONTHLY_LIMIT_USD` config var, not an API — Anthropic's Spend Limits API is Enterprise-only (not available to Console organizations). Keep it in sync with your tier cap on the Console *Rate limits* page.
- **(v3) Credit balance** is not shown — the Admin API exposes no credit-balance endpoint, so the firmware hides that field and the burn-rate value spans full width.
- **(v3) Month-to-date spend** can lag real usage by up to a few hours (Admin API cost data refreshes periodically) and is polled only every 15 minutes.
