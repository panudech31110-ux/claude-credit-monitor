#include "layout.h"
#include "sparkline.h"
#include <stdio.h>
#include <string.h>

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

// ── Rate-limit bar row ────────────────────────────────────────────────────────

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
  const int textY = barY + (RL_CELL_H - 8) / 2;  // centre FONT_SM in bar height
  disp.drawText(4, textY, lbuf, FONT_SM);

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
  disp.drawText(RL_VAL_X, textY, vbuf, FONT_SM);
}

// ── Layout ────────────────────────────────────────────────────────────────────
//
// y=  0.. 21  Header
// y= 22..114  TODAY / WEEK  (day cost + token shorthand)
// y=115..169  24h counter grid
// y=170..229  Rate-limit bars  (only if rl_valid)
// y=230..299  Sparkline  (expands to y=170..299 if !rl_valid)

void renderLayout(DisplayST7305& disp, const StatsData& data) {
  disp.clear();

  const bool hasRL = data.rl_valid;

  // ── Separators ──────────────────────────────────────────────────────────────
  disp.drawHLine(0,  21, LCD_W, true);
  disp.drawHLine(0, 114, LCD_W, true);
  disp.drawHLine(0, 169, LCD_W, true);
  if (hasRL)
    disp.drawHLine(0, 229, LCD_W, true);

  // ── Header (y=0..21) ────────────────────────────────────────────────────────
  disp.drawText(4, 7, "CLAUDE USAGE", FONT_SM);
  {
    char tbuf[12];
    if (data.fetch_error)
      snprintf(tbuf, sizeof(tbuf), "%s !", data.timestamp);
    else
      strncpy(tbuf, data.timestamp, sizeof(tbuf));
    disp.drawText(LCD_W - 4 - (int)strlen(tbuf) * 6, 7, tbuf, FONT_SM);
  }

  // ── TODAY / WEEK (y=22..114) ────────────────────────────────────────────────
  // Vertical divider between the two columns
  disp.fillRect(200, 22, 1, 93, true);

  const int COL_L = 8;
  const int COL_R = 210;

  disp.drawText(COL_L, 28, "TODAY", FONT_SM);
  disp.drawText(COL_R, 28, "WEEK",  FONT_SM);

  // Cost (FONT_MD, 16px)
  {
    char buf[12];
    fmtCost(buf, sizeof(buf), data.day.cost_usd);
    disp.drawText(COL_L, 46, buf, FONT_MD);
    fmtCost(buf, sizeof(buf), data.week.cost_usd);
    disp.drawText(COL_R, 46, buf, FONT_MD);
  }

  // Token shorthand (FONT_MD)
  {
    char sbuf[10], tbuf[16];
    fmtShort(sbuf, sizeof(sbuf), data.day.in_tokens + data.day.out_tokens);
    snprintf(tbuf, sizeof(tbuf), "%s tok", sbuf);
    disp.drawText(COL_L, 70, tbuf, FONT_MD);

    fmtShort(sbuf, sizeof(sbuf), data.week.in_tokens + data.week.out_tokens);
    snprintf(tbuf, sizeof(tbuf), "%s tok", sbuf);
    disp.drawText(COL_R, 70, tbuf, FONT_MD);
  }

  // ── 24h counter grid (y=115..169) ──────────────────────────────────────────
  {
    char buf[20];
    const int L  = 46;    // left col x (after "24h" label)
    const int R  = 224;   // right col x
    const int r1 = 122;
    const int r2 = 146;

    disp.drawText(4, r1 + 4, "24h", FONT_SM);

    disp.drawText(L, r1, "in:", FONT_MD);
    fmtNum(buf, sizeof(buf), data.in_tokens);
    disp.drawText(L + 4 * 12, r1, buf, FONT_MD);

    disp.drawText(L, r2, "out:", FONT_MD);
    fmtNum(buf, sizeof(buf), data.out_tokens);
    disp.drawText(L + 4 * 12, r2, buf, FONT_MD);

    disp.drawText(R, r1, "cr:", FONT_MD);
    fmtNum(buf, sizeof(buf), data.cache_read);
    disp.drawText(R + 4 * 12, r1, buf, FONT_MD);

    disp.drawText(R, r2, "cc:", FONT_MD);
    fmtNum(buf, sizeof(buf), data.cache_creation);
    disp.drawText(R + 4 * 12, r2, buf, FONT_MD);
  }

  // ── Rate-limit bars (y=170..229) — only if rl_valid ────────────────────────
  if (hasRL) {
    drawRlRow(disp, 173, "RPM",  data.rl_rpm.lim,  data.rl_rpm.rem);
    drawRlRow(disp, 193, "ITPM", data.rl_itpm.lim, data.rl_itpm.rem);
    drawRlRow(disp, 213, "OTPM", data.rl_otpm.lim, data.rl_otpm.rem);
  }

  // ── Sparkline ───────────────────────────────────────────────────────────────
  const int sparkY = hasRL ? 232 : 172;
  const int sparkH = hasRL ?  68 : 128;
  drawSparkline(disp, data.spark, 7, 0, sparkY, LCD_W, sparkH, data.today_dow);

  disp.refresh();
}
