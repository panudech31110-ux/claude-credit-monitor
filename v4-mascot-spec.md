# v4: Claude usage monitor — big mascot + 3 boxed usage cards

Replaces the earlier v4 dog spec. Pivots from "API spend monitor" to "Claude Code usage monitor" using ccusage as the data source.

## Architecture

```
Claude Code CLI writes JSONL
        ↓ (your laptop)
ccusage --json
        ↓
bridge.py  → POST /ingest-cc  → Cloudflare Worker
                                        ↓
                                    D1 singleton
                                        ↓
                                    /stats "cc" block
                                        ↓
                                    ESP32 (polls every 2 min)
```

Three new pieces: bridge script, Worker endpoint, firmware layout. The existing proxy stays (its month data is relegated to a status line at the bottom — "fallback" source).

## Part 1 — Laptop bridge script (new)

File: `bridge/bridge.py`. Depends on `requests` and `ccusage` globally installed (`npm install -g ccusage`).

### Behavior

Every 5 minutes (via Windows Task Scheduler):

1. Run `ccusage blocks --json` → parse the active 5-hour block
2. Run `ccusage daily --json` → sum today's total
3. Run `ccusage daily --since <monday> --until <today> --json` → sum this calendar week (Mon-Sun Bangkok)
4. Compute percentages against configurable plan limits
5. POST JSON to Worker's new `/ingest-cc` endpoint with `X-Ingest-Auth` header
6. Log result to `bridge.log` (append-only, rotate at 10 MB)

### Config (`bridge/.env`)

```
WORKER_URL=https://claude-credit-proxy.panudech31110.workers.dev
INGEST_SECRET=<32-char random hex, matches Worker>
PLAN_SESSION_LIMIT_USD=18.00     # Pro's approx per-5h soft limit
PLAN_DAILY_LIMIT_USD=25.00       # user's chosen daily cap
PLAN_WEEKLY_LIMIT_USD=120.00     # user's chosen weekly cap
TIMEZONE=Asia/Bangkok
```

Limits are configurable so the user tunes them to their actual plan + usage pattern. Store `.env` next to the script, add to `.gitignore`.

### POST payload shape

```json
{
  "session": {
    "cost": 9.00, "pct": 0.50,
    "reset_at": 1795800000, "tokens": 145000
  },
  "today": {
    "cost": 2.75, "pct": 0.11,
    "reset_at": 1795830000, "tokens": 48000
  },
  "week": {
    "cost": 30.00, "pct": 0.25,
    "reset_at": 1796100000, "tokens": 420000
  }
}
```

### Windows Task Scheduler entry

Document in README how to create a scheduled task:
- Trigger: every 5 minutes
- Action: `pythonw.exe bridge/bridge.py` (pythonw suppresses console flash)
- Start in: project folder
- Run whether user is logged in or not: optional (lets it survive lock screen)

## Part 2 — Worker changes

### New D1 table

```sql
CREATE TABLE cc_usage (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  updated_at INTEGER NOT NULL,
  session_cost REAL, session_pct REAL, session_reset_at INTEGER, session_tokens INTEGER,
  today_cost REAL, today_pct REAL, today_reset_at INTEGER, today_tokens INTEGER,
  week_cost REAL, week_pct REAL, week_reset_at INTEGER, week_tokens INTEGER
);
INSERT OR IGNORE INTO cc_usage (id, updated_at) VALUES (1, 0);
```

Apply as `schema_v4.sql`.

### New secret

```
wrangler secret put INGEST_SECRET
```

Same value stored in bridge `.env`.

### New endpoint — POST /ingest-cc

- Verify `X-Ingest-Auth` header matches `INGEST_SECRET`; 401 if missing/wrong
- Parse JSON body (validate shape)
- Upsert singleton row in `cc_usage` with `updated_at = now()`
- Return `{"ok": true}` on success

### Extend /stats response

Add `cc` block (only present when `cc_usage.updated_at > 0` AND `now - updated_at < 15 min` — stale data is hidden):

```json
"cc": {
  "updated": 1795754000,
  "session": { "pct": 0.50, "cost": 9.00, "reset_at": 1795800000 },
  "today":   { "pct": 0.11, "cost": 2.75, "reset_at": 1795830000 },
  "week":    { "pct": 0.25, "cost": 30.00, "reset_at": 1796100000 }
}
```

If `cc` block is absent, firmware shows "bridge offline" state (greyed-out cards with "—" instead of percentages).

## Part 3 — Firmware layout (400×300)

Two-column layout: mascot on the LEFT, three boxed cards stacked on the RIGHT.

```
padding 10px around edges

+-------------------------------------------------+ y=0
|                                                 |
|  +----------+   +----------------------------+  |
|  |          |   | CURRENT              50%  |  |
|  |          |   | [########░░░░░░░░░░░░░░░]  |  |  Card 1: 80px
|  |          |   | Resets in 1h 22m          |  |
|  |    ✦     |   +----------------------------+  |
|  |   ✦✦✦    |                                  |
|  |  ✦ C ✦   |   +----------------------------+  |
|  |   ✦✦✦    |   | TODAY                11%  |  |
|  |    ✦     |   | [##░░░░░░░░░░░░░░░░░░░░]   |  |  Card 2: 80px
|  |          |   | Resets in 8h 15m          |  |
|  | mascot   |   +----------------------------+  |
|  | 130 wide |                                  |
|  | 230 tall |   +----------------------------+  |
|  |          |   | WEEKLY               25%  |  |
|  |          |   | [#####░░░░░░░░░░░░░░░░░]   |  |  Card 3: 80px
|  |          |   | Resets in 4d 8h           |  |
|  +----------+   +----------------------------+  |
|                                                 |
+-------------------------------------------------+ y=300
```

### Precise geometry

- **Outer padding:** 10 px on all four sides
- **Mascot area:** x=10 to x=140, y=10 to y=290 (130 × 280 px)
- **Column gap:** x=140 to x=150 (10 px)
- **Card area:** x=150 to x=390 (240 px wide)
- **Cards:** each 240 × 80 px, with 10 px gap between
  - Card 1: y=15 to y=95
  - Card 2: y=100 to y=180
  - Card 3: y=185 to y=265
- Bottom status strip (optional, if space): y=270 to y=290 for "Month $16.50/$500" fallback line in 10 px font

### Box rendering (each card)

- 1 px border rectangle around the full 240 × 80 region
- Inner padding 6 px
- Three text rows inside:

### Consistent font sizes (applied across ALL three cards)

- **Label** (CURRENT / TODAY / WEEKLY): 14 px, top-left of card
- **Percentage** (50%): 20 px, top-right of card, right-aligned — only font size that's bigger, because it's the primary value
- **Bar**: 10 px tall, 220 px wide, below the label row
- **Reset line** (Resets in Xh YYm): 12 px, bottom row

You asked for consistent font sizes. All labels at 14 px, all percentages at 20 px, all reset lines at 12 px. The percentage is slightly larger because it carries the main meaning — if you want it also at 14 px for strict uniformity, say so and I'll adjust.

### Percentage formatting

- `< 1%` → `<1%`
- `1-99%` → integer, e.g. `50%`
- `>= 100%` → `>100%`

### Reset countdown formatting

- Less than 1 day: `Resets in Xh YYm` (e.g. `Resets in 1h 22m`)
- Less than 1 hour: `Resets in YYm`
- More than 1 day: `Resets in Xd Yh` (e.g. `Resets in 4d 8h`)
- Stale/unknown: `Resets in —`

### Bar fill rules

- Fill = min(1.0, pct)
- Fully filled when pct >= 100%
- When pct >= 80%: fill + 1 px border around the whole bar as warning
- When pct >= 100%: fill + inverted outline (visually "overrun")

### Offline state (no `cc` block in /stats)

- All three cards show `—` instead of percentage
- Bars drawn as empty outlines
- Reset lines show `Bridge offline`
- Mascot enters "sleeping" state

## Part 4 — Claude mascot

### Sprite

**100 × 100 px** (bigger than v3 dog, fits the dedicated 130-wide column). Centered in the mascot area at (x=20, y=80).

The Claude starburst: 12 symmetric radial lines ("rays") emanating from a center point. Each ray is a tapered shape — wider at the base (center), narrower at the tip. Classic look from the reference image you sent.

**Base geometry for Claude Code to generate:**
- Center at (50, 50) within the 100×100 sprite
- 12 rays at 30° increments (0°, 30°, 60°, ...)
- Each ray: 40 px long from center to tip
- Each ray: 8 px wide at base (near center), 2 px wide at tip
- Rendered as filled polygons in pixel art

### State-driven animation

| State | Condition | Behavior |
|---|---|---|
| **sleeping** | bridge offline, or all pcts < 5% | Shrunk to center dot only + `Zzz` text below |
| **idle** | all pcts < 25% | Gentle pulse: rays grow/shrink by 2 px every 3 s |
| **active** | any pct 25-60% | Slow rotation: rotate 15° every 3 s |
| **busy** | any pct 60-80% | Faster rotation: 15° every 1 s |
| **critical** | any pct >= 80% | Fast pulse + rotation: 30° every 0.5 s |
| **worried** | any pct >= 100% | Inverted (white rays on black background) + wobble |

**Status label below mascot** (at y=215, centered, 12 px): one word, matches state — `sleeping`, `idle`, `active`, `busy`, `critical`, or `worried`.

### Animation implementation

Since 12 rays are symmetric, rotation = draw the same sprite at different angles. Pre-compute 6 rotation frames (0°, 5°, 10°, 15°, 20°, 25°) stored as PROGMEM bitmaps. 100×100 = 1250 bytes/frame × 6 frames = 7.5 KB flash. Trivial.

Use partial ST7305 refresh for the 100×100 sprite region so the animation doesn't trigger full-screen redraw.

If partial refresh looks bad (ghosting), fall back to static mascot that only updates on state change.

## Part 5 — Build order — stop after each

1. **Worker: schema_v4.sql** — create `cc_usage` table, verify singleton row.
2. **Worker: /ingest-cc endpoint + /stats extension** — write code, deploy, verify with `curl` POST and `curl` GET.
3. **Bridge script scaffold** — Python script that parses ccusage output and prints the POST payload (don't send yet). Run manually, confirm JSON looks right.
4. **Bridge script sending** — add POST call, run manually, verify `cc_usage` D1 row updates.
5. **Windows Task Scheduler setup** — document in README, test by waiting for a 5-min cycle.
6. **Firmware: layout skeleton** — mascot column + 3 boxed cards with placeholder data. Flash and confirm proportions + box borders.
7. **Firmware: real card data** — pull `cc` block from `/stats`, render with real percentages. Handle missing `cc` → offline state.
8. **Firmware: static mascot** — draw the 100×100 Claude starburst at idle. Send a photo for review.
9. **Firmware: animation** — add rotation/pulse states. Start with idle + sleeping only. If stable, add the full state machine.
10. **README update** — document the full bridge setup for future-you (install ccusage, generate INGEST_SECRET, create Task Scheduler entry, test).

## Don't

- Don't make the bridge script use any Anthropic keys — it only needs `INGEST_SECRET` and reads local ccusage output.
- Don't poll faster than every 5 minutes — Claude Code's JSONL writes are not real-time anyway.
- Don't drop the proxy — its month data still provides the fallback status line.
- Don't make the mascot larger than 100×100 — eats the box widths.
- Don't try to show Weekly from the undocumented `anthropic-ratelimit-unified-7d-utilization` header — out of scope; computed-from-ccusage is good enough.

## Ask me before

- Changing the plan limits to be auto-detected (currently manual in `bridge/.env`).
- Changing the bridge language from Python to PowerShell or Node.
- Removing the fallback status line.
- Using the Claude starburst anywhere other than this personal device (trademark considerations).
