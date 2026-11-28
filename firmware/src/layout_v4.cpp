#include "layout_v4.h"
#include "mascot.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

// ── Geometry (v4-mascot-spec.md, "Precise geometry") ─────────────────────────
// Outer padding 10 px. Mascot column x=10..140, cards x=150..390.
static const int MASCOT_X = 10, MASCOT_Y = 10, MASCOT_W = 130, MASCOT_H = 280;
static const int CARD_X = 150, CARD_W = 240, CARD_H = 80;
static const int CARD_Y[3] = {15, 100, 185};
static const int STRIP_Y = 270;               // month fallback line, y=270..290

// Inside a card: 1 px border, 6 px vertical padding; the 220 px bar is centred,
// and the label / % / reset text align to the bar's left and right edges.
static const int BAR_W = 220, BAR_H = 10;
static const int PAD_X = (CARD_W - BAR_W) / 2;  // 10 px from the card's outer edge

// Font scales for the 5×8 bitmap font (cap height = 7 px × scale):
//   label   ×2 → 14 px  (spec 14)
//   percent ×3 → 21 px  (spec 20)
//   reset   ×2 → 14 px  (spec 12; ×1 would be 7 px, too small to read)
//   strip   ×1 →  7 px  (spec 10)
static const int S_LABEL = 2, S_PCT = 3, S_RESET = 2, S_STRIP = 1;
static const int CAP = 7;   // glyph cap height in font rows

// Vertical positions relative to the card's top edge (cap tops).
static const int Y_PCT   = 7;                               // 1 px border + 6 px pad
static const int Y_LABEL = Y_PCT + CAP * S_PCT - CAP * S_LABEL;  // share the % baseline
static const int Y_BAR   = 38;
static const int Y_RESET = CARD_H - 1 - 6 - CAP * S_RESET;  // bottom pad 6 px

// ── Text helpers ──────────────────────────────────────────────────────────────

static int textWidth(const char* s, int scale) {
  int n = (int)strlen(s);
  return n > 0 ? n * 6 * scale - scale : 0;  // drop trailing 1-column gap
}

static void drawTextRight(DisplayST7305& d, int right, int y, const char* s, int scale) {
  d.drawText(right - textWidth(s, scale), y, s, scale);
}

// "<1%" | "50%" | ">100%"  (spec: Percentage formatting)
static void fmtPct(char* buf, size_t sz, float ratio) {
  float pct = ratio * 100.0f;
  if (pct < 1.0f)         snprintf(buf, sz, "<1%%");
  else if (pct >= 100.0f) snprintf(buf, sz, ">100%%");
  else                    snprintf(buf, sz, "%d%%", (int)floorf(pct));
}

// "Resets in 4d 8h" | "Resets in 1h 22m" | "Resets in 22m" | "Resets in -"
// The bitmap font is ASCII-only, so the spec's em dash is drawn as "-".
static void fmtReset(char* buf, size_t sz, long reset_at, long now) {
  long left = reset_at - now;
  if (reset_at <= 0 || left <= 0) { snprintf(buf, sz, "Resets in -"); return; }
  long d = left / 86400, h = (left % 86400) / 3600, m = (left % 3600) / 60;
  if (d >= 1)      snprintf(buf, sz, "Resets in %ldd %ldh", d, h);
  else if (h >= 1) snprintf(buf, sz, "Resets in %ldh %02ldm", h, m);
  else             snprintf(buf, sz, "Resets in %ldm", m);
}

// "Resets at 00:00" — reset time as a local (Bangkok, set by configTime) 24h clock.
static void fmtResetClock(char* buf, size_t sz, long reset_at) {
  time_t t = (time_t)reset_at;
  struct tm tmv;
  localtime_r(&t, &tmv);
  snprintf(buf, sz, "Resets at %02d:%02d", tmv.tm_hour, tmv.tm_min);
}

// The reset line a card shows. Offline / unknown → "Resets in -" for every card.
static void fmtCardReset(char* buf, size_t sz, const V4Card& c, long now) {
  if (c.valid && c.reset_clock && c.reset_at > 0) fmtResetClock(buf, sz, c.reset_at);
  else fmtReset(buf, sz, c.valid ? c.reset_at : 0, now);
}

// ── Bar (spec: Bar fill rules) ───────────────────────────────────────────────
//   always: 1 px outline, fill proportional to min(1, pct)
//   >= 80%: extra 1 px ring 2 px outside the bar (warning)
//   >=100%: fully filled with an inverted (white) inner outline (overrun)
//   offline: empty outline only
static void drawBar(DisplayST7305& d, int x, int y, bool valid, float ratio) {
  d.drawRect(x, y, BAR_W, BAR_H, true);
  if (!valid) return;

  if (ratio >= 0.8f) d.drawRect(x - 2, y - 2, BAR_W + 4, BAR_H + 4, true);

  if (ratio >= 1.0f) {
    d.fillRect(x, y, BAR_W, BAR_H, true);
    d.drawRect(x + 2, y + 2, BAR_W - 4, BAR_H - 4, false);
    return;
  }
  float f = ratio < 0.0f ? 0.0f : ratio;
  int fw = (int)(f * (BAR_W - 2) + 0.5f);
  if (fw > 0) d.fillRect(x + 1, y + 1, fw, BAR_H - 2, true);
}

// ── Card ──────────────────────────────────────────────────────────────────────

static void drawCard(DisplayST7305& d, int cy, const V4Card& c, long now) {
  const int left  = CARD_X + PAD_X;
  const int right = CARD_X + CARD_W - PAD_X;

  d.drawRect(CARD_X, cy, CARD_W, CARD_H, true);

  d.drawText(left, cy + Y_LABEL, c.label, S_LABEL);

  char pbuf[8];
  if (c.valid) fmtPct(pbuf, sizeof(pbuf), c.pct);
  else         snprintf(pbuf, sizeof(pbuf), "-");
  drawTextRight(d, right, cy + Y_PCT, pbuf, S_PCT);

  drawBar(d, left, cy + Y_BAR, c.valid, c.pct);

  char rbuf[24];
  fmtCardReset(rbuf, sizeof(rbuf), c, now);
  d.drawText(left, cy + Y_RESET, rbuf, S_RESET);
}

// ── Month fallback strip ──────────────────────────────────────────────────────

static void fmtDollar(char* buf, size_t sz, float usd) {
  if (usd >= 100.0f) snprintf(buf, sz, "$%.0f", (double)usd);
  else               snprintf(buf, sz, "$%.2f", (double)usd);
}

// "Month $16.50/$500  Next: 1/Dec/2026" — next reset = 1st of next month, local
// (Bangkok) time. At ×1 (6 px/char) the text column (x=160..380) fits 36 chars;
// the longest realistic line ("Month $99.99/$1000  Next: 1/Dec/2026") is exactly
// 36, while "Next reset" would need 41, so the short label is used.
// Before NTP sync the date is left off.
static void fmtStrip(char* buf, size_t sz, const V4Data& data, long now) {
  if (!data.month_valid) { buf[0] = '\0'; return; }
  char u[12], l[12];
  fmtDollar(u, sizeof(u), data.month_usd);
  fmtDollar(l, sizeof(l), data.month_lim);
  if (now <= 1700000000L) { snprintf(buf, sz, "Month %s/%s", u, l); return; }

  static const char* const MON[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  time_t t = (time_t)now;
  struct tm tmv;
  localtime_r(&t, &tmv);
  int mon = tmv.tm_mon + 1, year = tmv.tm_year + 1900;   // month after this one
  if (mon == 12) { mon = 0; year++; }
  snprintf(buf, sz, "Month %s/%s  Next: 1/%s/%d", u, l, MON[mon], year);
}

static void drawStrip(DisplayST7305& d, const V4Data& data, long now) {
  char line[48];
  fmtStrip(line, sizeof(line), data, now);
  // Vertically centred in y=270..290, aligned with the card text column.
  if (line[0]) d.drawText(CARD_X + PAD_X, STRIP_Y + (20 - CAP * S_STRIP) / 2, line, S_STRIP);
}

// ── Regions & partial refresh ────────────────────────────────────────────────
// The mascot column and the card column are redrawn and pushed independently
// (DisplayST7305::refreshColumns), so an animation frame never touches the
// cards and a card update never touches the mascot.
static const int MASCOT_REGION_X0 = 0,   MASCOT_REGION_X1 = 145;  // ends on an odd x
static const int CARDS_REGION_X0  = 146, CARDS_REGION_X1  = LCD_W - 1;

// Set to 0 if partial pushes misbehave on the panel (wrong area / ghosting): every
// update then pushes the full 15 KB frame instead (~12 ms at 10 MHz SPI). The
// change detection above still applies either way.
#ifndef V4_PARTIAL_REFRESH
#define V4_PARTIAL_REFRESH 1
#endif

static void pushColumns(DisplayST7305& d, int x0, int x1) {
#if V4_PARTIAL_REFRESH
  d.refreshColumns(x0, x1);
#else
  (void)x0; (void)x1;
  d.refresh();
#endif
}

// ── Card change detection ─────────────────────────────────────────────────────
// A "signature" of exactly what the card column would show: % text, bar fill
// and warning state, reset line, and the Month strip. Cards are redrawn only
// when it changes — i.e. on new data, or when a countdown ticks to a new minute.

static char lastCardSig[256] = "";

static void cardSignature(char* buf, size_t sz, const V4Data& v, long now) {
  int n = 0;
  for (int i = 0; i < 3 && n < (int)sz; i++) {
    const V4Card& c = v.cards[i];
    char p[8], r[24];
    if (c.valid) fmtPct(p, sizeof(p), c.pct); else snprintf(p, sizeof(p), "-");
    fmtCardReset(r, sizeof(r), c, now);
    const int fill = !c.valid ? -1 : c.pct >= 1.0f ? -2
                   : (int)((c.pct < 0 ? 0 : c.pct) * (BAR_W - 2) + 0.5f);
    n += snprintf(buf + n, sz - n, "%s|%d|%d|%s;", p, fill, c.valid && c.pct >= 0.8f, r);
  }
  char s[48];
  fmtStrip(s, sizeof(s), v, now);
  if (n < (int)sz) snprintf(buf + n, sz - n, "%s", s);
}

static void drawCardsRegion(DisplayST7305& d, const V4Data& data, long now) {
  d.fillRect(CARDS_REGION_X0, 0, CARDS_REGION_X1 - CARDS_REGION_X0 + 1, LCD_H, false);
  for (int i = 0; i < 3; i++) drawCard(d, CARD_Y[i], data.cards[i], now);
  drawStrip(d, data, now);
}

// ── Mascot column ─────────────────────────────────────────────────────────────
// 100×100 sprite centred horizontally in the 130 px column at the spec's y=80,
// one-word status label below it at y=215 (spec), centred.
static const int SPRITE_X = MASCOT_X + (MASCOT_W - MASCOT_SIZE) / 2;  // 25
static const int SPRITE_Y = 80;
static const int STATUS_Y = 215;
static const int S_STATUS = 2;   // 14 px cap, same as the card labels / reset lines

static MascotState autoState  = MascotState::Sleeping;  // from the latest data
static int         forcedState = -1;                    // serial test override
static MascotFrame lastFrame;
static bool        haveFrame = false;

static MascotState currentState() {
  return forcedState >= 0 ? (MascotState)forcedState : autoState;
}

static void updateAutoState(const V4Data& v) {
  bool online = false;
  float maxPct = 0.0f;
  for (int i = 0; i < 3; i++) {
    if (!v.cards[i].valid) continue;
    online = true;
    if (v.cards[i].pct > maxPct) maxPct = v.cards[i].pct;
  }
  autoState = mascotStateFor(online, maxPct);
}

static void drawMascotRegion(DisplayST7305& d, const MascotFrame& f) {
  // Critical is drawn inverted: black panel, white creature and label.
  const bool inv = (f.state == MascotState::Critical);
  const bool ink = !inv;

  d.fillRect(MASCOT_REGION_X0, 0, MASCOT_REGION_X1 - MASCOT_REGION_X0 + 1, LCD_H, false);
  if (inv) d.fillRect(MASCOT_X, MASCOT_Y, MASCOT_W, MASCOT_H, true);

  drawMascot(d, SPRITE_X, SPRITE_Y, f, ink);

  if (f.state == MascotState::Sleeping) {
    // "z" always, "Z" on alternate frames, top-right as in the design preview.
    d.drawText(SPRITE_X + 70, SPRITE_Y - 18, "z", 2, ink);
    if (f.zz) d.drawText(SPRITE_X + 84, SPRITE_Y - 40, "Z", 3, ink);
  }

  const char* status = mascotStateName(f.state);
  d.drawText(MASCOT_X + (MASCOT_W - textWidth(status, S_STATUS)) / 2, STATUS_Y, status,
             S_STATUS, ink);
}

// ── Public ────────────────────────────────────────────────────────────────────

void v4RenderFull(DisplayST7305& disp, const V4Data& data, long now, uint32_t ms) {
  updateAutoState(data);
  lastFrame = mascotFrameAt(currentState(), ms);
  haveFrame = true;
  cardSignature(lastCardSig, sizeof(lastCardSig), data, now);

  disp.clear();
  drawMascotRegion(disp, lastFrame);
  drawCardsRegion(disp, data, now);
  disp.refresh();
}

bool v4UpdateCards(DisplayST7305& disp, const V4Data& data, long now) {
  updateAutoState(data);   // the mascot picks this up on its next tick
  char sig[sizeof(lastCardSig)];
  cardSignature(sig, sizeof(sig), data, now);
  if (strcmp(sig, lastCardSig) == 0) return false;
  memcpy(lastCardSig, sig, sizeof(sig));
  drawCardsRegion(disp, data, now);
  pushColumns(disp, CARDS_REGION_X0, CARDS_REGION_X1);
  return true;
}

bool v4TickMascot(DisplayST7305& disp, uint32_t ms) {
  const MascotFrame f = mascotFrameAt(currentState(), ms);
  if (haveFrame && f == lastFrame) return false;
  lastFrame = f;
  haveFrame = true;
  drawMascotRegion(disp, f);
  pushColumns(disp, MASCOT_REGION_X0, MASCOT_REGION_X1);
  return true;
}

void v4ForceMascotState(int state) {
  forcedState = (state >= 0 && state < MASCOT_STATE_COUNT) ? state : -1;
}

const char* v4MascotStateName() { return mascotStateName(currentState()); }

static V4Card makeCard(const char* label, bool valid, float pct, long reset_at) {
  V4Card c;
  c.label = label; c.valid = valid; c.pct = pct; c.reset_at = reset_at;
  return c;
}

// Same cutoff the Worker uses to hide the cc block. Re-checked here because the
// firmware keeps showing last-good data when a /stats fetch fails.
static const long CC_STALE_SECONDS = 15 * 60;

V4Data v4FromStats(const StatsData& s, long now) {
  const CcData& cc = s.cc;
  // `now` is only trusted once NTP has synced (otherwise it's ~1970).
  const bool clockOk = now > 1700000000L;
  const bool fresh = cc.valid && (!clockOk || now - cc.updated < CC_STALE_SECONDS);

  V4Data v;
  v.cards[0] = makeCard("CURRENT", fresh, cc.session.pct, cc.session.reset_at);
  v.cards[1] = makeCard("TODAY",   fresh, cc.today.pct,   cc.today.reset_at);
  v.cards[1].reset_clock = true;   // "Resets at 00:00" instead of a countdown
  v.cards[2] = makeCard("WEEKLY",  fresh, cc.week.pct,    cc.week.reset_at);
  v.month_valid = s.month.valid;
  v.month_usd   = s.month.usd;
  v.month_lim   = s.month.lim;
  return v;
}
