#include "layout_v4.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

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

  // Offline → reset_at treated as unknown → "Resets in -".
  char rbuf[24];
  fmtReset(rbuf, sizeof(rbuf), c.valid ? c.reset_at : 0, now);
  d.drawText(left, cy + Y_RESET, rbuf, S_RESET);
}

// ── Month fallback strip ──────────────────────────────────────────────────────

static void fmtDollar(char* buf, size_t sz, float usd) {
  if (usd >= 100.0f) snprintf(buf, sz, "$%.0f", (double)usd);
  else               snprintf(buf, sz, "$%.2f", (double)usd);
}

static void drawStrip(DisplayST7305& d, const V4Data& data) {
  if (!data.month_valid) return;
  char u[12], l[12], line[40];
  fmtDollar(u, sizeof(u), data.month_usd);
  fmtDollar(l, sizeof(l), data.month_lim);
  snprintf(line, sizeof(line), "Month %s/%s", u, l);
  // Vertically centred in y=270..290, aligned with the card text column.
  d.drawText(CARD_X + PAD_X, STRIP_Y + (20 - CAP * S_STRIP) / 2, line, S_STRIP);
}

// ── Public ────────────────────────────────────────────────────────────────────

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
  v.cards[2] = makeCard("WEEKLY",  fresh, cc.week.pct,    cc.week.reset_at);
  v.month_valid = s.month.valid;
  v.month_usd   = s.month.usd;
  v.month_lim   = s.month.lim;
  return v;
}

void renderV4Layout(DisplayST7305& disp, const V4Data& data, long now) {
  disp.clear();

  // Mascot column (MASCOT_X..+W, MASCOT_Y..+H) stays empty until step 8.
  (void)MASCOT_X; (void)MASCOT_Y; (void)MASCOT_W; (void)MASCOT_H;

  for (int i = 0; i < 3; i++) drawCard(disp, CARD_Y[i], data.cards[i], now);
  drawStrip(disp, data);

  disp.refresh();
}
