#include "sparkline.h"
#include <string.h>

// DOW → single letter, indexed by tm_wday (0=Sun)
static const char DOW_LETTER[] = "SMTWTFS";

// drawSparkline: renders 7 vertical bars in the rectangle (x, y, w, h).
// spark[0] = oldest (6 days ago), spark[6] = today.
// todayDow: tm_wday of today (0=Sun … 6=Sat).
// Today's bar (rightmost) is drawn as an outlined rectangle instead of filled.
void drawSparkline(DisplayST7305& disp,
                   const int* values, int count,
                   int x, int y, int w, int h,
                   int todayDow)
{
  // ── Find max ────────────────────────────────────────────────────────────────
  int maxVal = 0;
  for (int i = 0; i < count; i++) {
    if (values[i] > maxVal) maxVal = values[i];
  }

  const int DAY_H   = 10;         // height reserved below bars for day letters
  const int barAreaH = h - DAY_H; // actual bar draw area height (pixels)
  const int barBot  = y + barAreaH;
  const int dayY    = y + barAreaH + 2;

  for (int i = 0; i < count; i++) {
    // Bar x/w using integer division to fill exactly `w` pixels total
    int bx = x + (i * w) / count;
    int bw = x + ((i + 1) * w) / count - bx;
    bool isToday = (i == count - 1);

    // Bar height
    int barH = 0;
    if (maxVal > 0 && values[i] > 0) {
      barH = (int)((long)values[i] * barAreaH / maxVal);
      if (barH < 1) barH = 1;
    }

    // Draw bar
    if (barH > 0) {
      int barY = barBot - barH;
      if (isToday) {
        disp.drawRect(bx, barY, bw, barH, true);   // outlined
      } else {
        disp.fillRect(bx, barY, bw, barH, true);   // filled
      }
    }

    // Day letter — centered in the bar's column
    int dow = ((todayDow - (count - 1 - i)) % 7 + 7) % 7;
    char letter[2] = { DOW_LETTER[dow], '\0' };
    int lx = bx + (bw - 6) / 2;   // 6px = one char stride at size=1
    disp.drawText(lx, dayY, letter, FONT_SM);
  }
}
