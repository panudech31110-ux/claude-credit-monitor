#include "layout.h"
#include "sparkline.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// Font size multipliers (drawText: cell = 6*size wide, 8*size tall).
static const int F_SM = 1;   // 8px  — footer, sparkline label, projection
static const int F_MD = 2;   // 16px — section labels, header title
static const int F_LG = 3;   // 24px — big values (amount, pct, credit, burn)
static const int CH_LG = 6 * 3;  // advance width per char at F_LG

// ── Formatting helpers ────────────────────────────────────────────────────────

static void fmtNum(char* buf, size_t sz, long n) {
  if (n < 0) n = 0;
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%ld", n);
  int len = (int)strlen(tmp);
  int out = 0;
  for (int i = 0; i < len && out < (int)sz - 1; i++) {
    if (i > 0 && (len - i) % 3 == 0) buf[out++] = ',';
    buf[out++] = tmp[i];
  }
  buf[out] = '\0';
}

static void fmtCost(char* buf, size_t sz, float usd) {
  if (usd > 0.0f && usd < 0.01f)
    snprintf(buf, sz, "< $0.01");
  else
    snprintf(buf, sz, "$%.2f", usd);
}

// v3 dollar format:
//   == 0            → "$0.00"
//   > 0, < 0.01     → "< $0.01"
//   >= 0.01, < 100  → "$X.XX"
//   >= 100          → "$X"  (integer, no decimals — e.g. the "$500" cap)
static void fmtDollar(char* buf, size_t sz, float usd) {
  if (usd <= 0.0f)       snprintf(buf, sz, "$0.00");
  else if (usd < 0.01f)  snprintf(buf, sz, "< $0.01");
  else if (usd >= 100.0f) snprintf(buf, sz, "$%.0f", (double)usd);
  else                   snprintf(buf, sz, "$%.2f", (double)usd);
}

// v3 percent format from a ratio (usd/lim):
//   <= 0   → "0%"
//   < 1    → "<1%"
//   > 100  → ">100%"
//   else   → "N%" using floor
static void fmtPct(char* buf, size_t sz, float ratio) {
  float pct = ratio * 100.0f;
  if (pct <= 0.0f)        snprintf(buf, sz, "0%%");
  else if (pct < 1.0f)    snprintf(buf, sz, "<1%%");
  else if (pct > 100.0f)  snprintf(buf, sz, ">100%%");
  else                    snprintf(buf, sz, "%d%%", (int)floorf(pct));
}

// < 1000 → "123"  |  < 1M → "12K"  |  >= 1M → "1.2M"
static void fmtShort(char* buf, size_t sz, long n) {
  if (n < 0) n = 0;
  if (n < 1000)
    snprintf(buf, sz, "%ld", n);
  else if (n < 1000000)
    snprintf(buf, sz, "%ldK", (n + 500) / 1000);
  else
    snprintf(buf, sz, "%.1fM", (double)n / 1000000.0);
}

// Small WiFi/signal-strength glyph (3 ascending bars), ~11px wide, 10px tall.
static void drawWifiIcon(DisplayST7305& disp, int x, int y) {
  disp.fillRect(x,     y + 6, 3, 4,  true);
  disp.fillRect(x + 4, y + 3, 3, 7,  true);
  disp.fillRect(x + 8, y,     3, 10, true);
}

// ── Rate-limit bar row (unchanged from v2) ────────────────────────────────────

static const int RL_BAR_X    = 38;
static const int RL_CELL_W   = 13;
static const int RL_CELL_H   = 14;
static const int RL_CELL_GAP = 2;
static const int RL_N_CELLS  = 10;
// total bar width: 10×13 + 9×2 = 148px
static const int RL_BAR_W  = RL_N_CELLS * (RL_CELL_W + RL_CELL_GAP) - RL_CELL_GAP;
static const int RL_VAL_X  = RL_BAR_X + RL_BAR_W + 8;  // 194

static void drawRlRow(DisplayST7305& disp, int barY, const char* label,
                      long lim, long rem) {
  if (lim <= 0) return;

  float ratio = (float)rem / (float)lim;
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  int filled = (int)(ratio * RL_N_CELLS + 0.5f);

  const bool warning  = (ratio < 0.5f);
  const bool critical = (ratio < 0.2f);

  // Label — "!" prefix on critical
  char lbuf[8];
  if (critical)
    snprintf(lbuf, sizeof(lbuf), "!%s", label);
  else
    strncpy(lbuf, label, sizeof(lbuf));
  const int textY = barY + (RL_CELL_H - 8) / 2;  // centre F_SM in bar height
  disp.drawText(4, textY, lbuf, F_SM);

  // Bar cells
  if (critical) {
    // Invert: solid black background, white cutouts for filled cells
    disp.fillRect(RL_BAR_X, barY, RL_BAR_W, RL_CELL_H, true);
    for (int i = 0; i < RL_N_CELLS; i++) {
      int cx = RL_BAR_X + i * (RL_CELL_W + RL_CELL_GAP);
      if (i < filled)
        disp.fillRect(cx + 2, barY + 2, RL_CELL_W - 4, RL_CELL_H - 4, false);
    }
  } else {
    for (int i = 0; i < RL_N_CELLS; i++) {
      int cx = RL_BAR_X + i * (RL_CELL_W + RL_CELL_GAP);
      if (i < filled) {
        disp.fillRect(cx, barY, RL_CELL_W, RL_CELL_H, true);
      } else if (warning) {
        disp.drawRect(cx, barY, RL_CELL_W, RL_CELL_H, true);
      }
      // >= 0.5: empty cells invisible
    }
  }

  // Value "rem/lim" using K/M shorthand
  char vbuf[20], rembuf[8], limbuf[8];
  fmtShort(rembuf, sizeof(rembuf), rem);
  fmtShort(limbuf, sizeof(limbuf), lim);
  snprintf(vbuf, sizeof(vbuf), "%s/%s", rembuf, limbuf);
  disp.drawText(RL_VAL_X, textY, vbuf, F_SM);
}

// Draw a label (F_MD) + value (F_LG) column, clamped to the right margin so
// long values never run off-screen.
static void drawStatCol(DisplayST7305& disp, int x, int labelY, int valY,
                        const char* label, const char* value) {
  disp.drawText(x, labelY, label, F_MD);
  int w  = (int)strlen(value) * CH_LG;
  int vx = x;
  if (vx + w > LCD_W - 6) vx = LCD_W - 6 - w;  // keep inside right margin
  if (vx < 4) vx = 4;
  disp.drawText(vx, valY, value, F_LG);
}

// ── v3 layout (Admin API month data present) ──────────────────────────────────

static void renderMonthLayout(DisplayST7305& disp, const StatsData& d) {
  const MonthData& m = d.month;
  const bool hasRL     = d.rl_valid;
  const bool hasCredit = (m.credit > 0.0f);

  const float ratio = (m.lim > 0.0f) ? (m.usd / m.lim) : 0.0f;
  const float pct   = ratio * 100.0f;

  // ── Header (y=0..23) ────────────────────────────────────────────────────────
  drawWifiIcon(disp, 6, 6);
  {
    const char* title = "CLAUDE API MONITOR";
    int tw = (int)strlen(title) * (6 * F_MD);
    disp.drawText((LCD_W - tw) / 2, 4, title, F_MD);
  }
  {
    char hbuf[16];
    snprintf(hbuf, sizeof(hbuf), "Day %d / %d", m.day_of_month, m.days_in_month);
    int hw = (int)strlen(hbuf) * (6 * F_SM);
    disp.drawText(LCD_W - 8 - hw, 8, hbuf, F_SM);
  }
  disp.drawHLine(0, 23, LCD_W, true);

  // ── Monthly budget block (y=26..129) ────────────────────────────────────────
  {
    // Label — "!" alert prefix when >= 80%
    char lbuf[20];
    snprintf(lbuf, sizeof(lbuf), "%sMONTHLY BUDGET", (pct >= 80.0f) ? "!" : "");
    disp.drawText(12, 30, lbuf, F_MD);

    // Amount "$usd / $lim"
    char ubuf[12], libuf[12], abuf[28];
    fmtDollar(ubuf, sizeof(ubuf), m.usd);
    fmtDollar(libuf, sizeof(libuf), m.lim);
    snprintf(abuf, sizeof(abuf), "%s / %s", ubuf, libuf);
    disp.drawText(12, 52, abuf, F_LG);

    // Percentage, right-aligned
    char pbuf[10];
    fmtPct(pbuf, sizeof(pbuf), ratio);
    int pw = (int)strlen(pbuf) * CH_LG;
    disp.drawText(LCD_W - 12 - pw, 52, pbuf, F_LG);

    // Big bar: 380×18 at x=10, y=92
    const int BX = 10, BY = 92, BW = 380, BH = 18;
    disp.drawRect(BX, BY, BW, BH, true);
    if (pct >= 100.0f) {
      // Pinned: fully filled with an inverted (white) inner outline
      disp.fillRect(BX, BY, BW, BH, true);
      disp.drawRect(BX + 2, BY + 2, BW - 4, BH - 4, false);
    } else {
      float f = ratio;
      if (f < 0.0f) f = 0.0f;
      if (f > 1.0f) f = 1.0f;
      int fw = (int)(f * (BW - 2) + 0.5f);
      if (fw > 0) disp.fillRect(BX + 1, BY + 1, fw, BH - 2, true);
    }

    // Projection row (F_SM for width safety across large dollar values)
    char prbuf[16], prline[48], ppct[10];
    fmtDollar(prbuf, sizeof(prbuf), m.projected_eom_usd);
    fmtPct(ppct, sizeof(ppct), m.projected_eom_pct);
    snprintf(prline, sizeof(prline), "Projected EOM: %s  (%s of cap)", prbuf, ppct);
    disp.drawText(12, 116, prline, F_SM);
  }
  disp.drawHLine(0, 130, LCD_W, true);

  // ── Credit + burn-rate strip (y=132..189) ───────────────────────────────────
  {
    // Burn rate = projected_eom_usd / days_in_month  (== usd / days_elapsed)
    float burn = (m.days_in_month > 0)
                   ? (m.projected_eom_usd / (float)m.days_in_month)
                   : 0.0f;
    char burnVal[20], burnNum[12];
    fmtDollar(burnNum, sizeof(burnNum), burn);
    snprintf(burnVal, sizeof(burnVal), "%s / day", burnNum);

    if (hasCredit) {
      char credVal[12];
      fmtDollar(credVal, sizeof(credVal), m.credit);
      disp.fillRect(200, 134, 1, 54, true);  // column divider
      drawStatCol(disp, 12,  140, 160, "CREDIT BALANCE", credVal);
      drawStatCol(disp, 212, 140, 160, "BURN RATE",      burnVal);
    } else {
      // No credit data → burn rate spans the full width
      drawStatCol(disp, 12, 140, 160, "BURN RATE", burnVal);
    }
  }
  disp.drawHLine(0, 190, LCD_W, true);

  // ── Rate-limit bars (y=194..246) — RPM + ITPM only (OTPM dropped) ───────────
  int sparkTop = 252;
  if (hasRL) {
    drawRlRow(disp, 198, "RPM",  d.rl_rpm.lim,  d.rl_rpm.rem);
    drawRlRow(disp, 222, "ITPM", d.rl_itpm.lim, d.rl_itpm.rem);
    disp.drawHLine(0, 248, LCD_W, true);
  } else {
    // No rate-limit data → sparkline expands upward into this space
    sparkTop = 200;
  }

  // ── Sparkline (y=sparkTop..288) ─────────────────────────────────────────────
  disp.drawText(4, sparkTop, "7-day spend ($)", F_SM);
  {
    const int sy = sparkTop + 10;
    const int sh = 288 - sy;
    drawSparkline(disp, d.spark, 7, 0, sy, LCD_W, sh, d.today_dow);
  }

  // ── Footer (y=290..300) ─────────────────────────────────────────────────────
  disp.drawHLine(0, 289, LCD_W, true);
  {
    const char* foot = "User btn: next view    Boot: refresh";
    int fw = (int)strlen(foot) * (6 * F_SM);
    disp.drawText((LCD_W - fw) / 2, 292, foot, F_SM);
  }
}

// ── v2 fallback layout (no Admin API month data) ──────────────────────────────
// Shown only when /stats omits the "month" block (Admin API never succeeded).
// Identical to the v2 layout, with the header title swapped for a warning.

static void renderFallbackLayout(DisplayST7305& disp, const StatsData& data) {
  const bool hasRL = data.rl_valid;

  // Separators
  disp.drawHLine(0,  21, LCD_W, true);
  disp.drawHLine(0, 114, LCD_W, true);
  disp.drawHLine(0, 169, LCD_W, true);
  if (hasRL)
    disp.drawHLine(0, 229, LCD_W, true);

  // Header (y=0..21) — "! Admin API not configured" warning replaces the title
  disp.drawText(4, 7, "! Admin API not configured", F_SM);
  {
    char tbuf[12];
    if (data.fetch_error)
      snprintf(tbuf, sizeof(tbuf), "%s !", data.timestamp);
    else
      strncpy(tbuf, data.timestamp, sizeof(tbuf));
    disp.drawText(LCD_W - 4 - (int)strlen(tbuf) * 6, 7, tbuf, F_SM);
  }

  // TODAY / WEEK (y=22..114)
  disp.fillRect(200, 22, 1, 93, true);
  const int COL_L = 8;
  const int COL_R = 210;
  disp.drawText(COL_L, 28, "TODAY", F_SM);
  disp.drawText(COL_R, 28, "WEEK",  F_SM);

  {
    char buf[12];
    fmtCost(buf, sizeof(buf), data.day.cost_usd);
    disp.drawText(COL_L, 46, buf, F_MD);
    fmtCost(buf, sizeof(buf), data.week.cost_usd);
    disp.drawText(COL_R, 46, buf, F_MD);
  }

  {
    char sbuf[10], tbuf[16];
    fmtShort(sbuf, sizeof(sbuf), data.day.in_tokens + data.day.out_tokens);
    snprintf(tbuf, sizeof(tbuf), "%s tok", sbuf);
    disp.drawText(COL_L, 70, tbuf, F_MD);

    fmtShort(sbuf, sizeof(sbuf), data.week.in_tokens + data.week.out_tokens);
    snprintf(tbuf, sizeof(tbuf), "%s tok", sbuf);
    disp.drawText(COL_R, 70, tbuf, F_MD);
  }

  // 24h counter grid (y=115..169)
  {
    char buf[20];
    const int L  = 46;
    const int R  = 224;
    const int r1 = 122;
    const int r2 = 146;

    disp.drawText(4, r1 + 4, "24h", F_SM);

    disp.drawText(L, r1, "in:", F_MD);
    fmtNum(buf, sizeof(buf), data.in_tokens);
    disp.drawText(L + 4 * 12, r1, buf, F_MD);

    disp.drawText(L, r2, "out:", F_MD);
    fmtNum(buf, sizeof(buf), data.out_tokens);
    disp.drawText(L + 4 * 12, r2, buf, F_MD);

    disp.drawText(R, r1, "cr:", F_MD);
    fmtNum(buf, sizeof(buf), data.cache_read);
    disp.drawText(R + 4 * 12, r1, buf, F_MD);

    disp.drawText(R, r2, "cc:", F_MD);
    fmtNum(buf, sizeof(buf), data.cache_creation);
    disp.drawText(R + 4 * 12, r2, buf, F_MD);
  }

  // Rate-limit bars (y=170..229)
  if (hasRL) {
    drawRlRow(disp, 173, "RPM",  data.rl_rpm.lim,  data.rl_rpm.rem);
    drawRlRow(disp, 193, "ITPM", data.rl_itpm.lim, data.rl_itpm.rem);
    drawRlRow(disp, 213, "OTPM", data.rl_otpm.lim, data.rl_otpm.rem);
  }

  // Sparkline
  const int sparkY = hasRL ? 232 : 172;
  const int sparkH = hasRL ?  68 : 128;
  drawSparkline(disp, data.spark, 7, 0, sparkY, LCD_W, sparkH, data.today_dow);
}

// ── Entry point ───────────────────────────────────────────────────────────────

void renderLayout(DisplayST7305& disp, const StatsData& data) {
  disp.clear();
  if (data.month.valid)
    renderMonthLayout(disp, data);
  else
    renderFallbackLayout(disp, data);
  disp.refresh();
}
