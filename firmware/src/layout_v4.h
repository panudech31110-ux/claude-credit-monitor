#pragma once
#include "display_st7305.h"
#include "layout.h"

// v4 main view: mascot column (left) + three boxed usage cards (right) + a
// one-line month fallback strip. See v4-mascot-spec.md, Part 3.

struct V4Card {
  const char* label    = "";     // "CURRENT" / "TODAY" / "WEEKLY"
  bool        valid    = false;  // false → "—"-style offline card
  float       pct      = 0.0f;   // ratio: 0.5 = 50%
  long        reset_at = 0;      // unix seconds; 0 = unknown
};

struct V4Data {
  V4Card cards[3];
  bool   month_valid = false;    // bottom strip: "Month $16.50/$500"
  float  month_usd   = 0.0f;
  float  month_lim   = 0.0f;
};

// Maps /stats data to the view: cc block → cards (offline when absent or older
// than 15 min), month block → bottom strip.
V4Data v4FromStats(const StatsData& s, long now);

// Draws the full v4 view into the framebuffer and refreshes the panel.
void renderV4Layout(DisplayST7305& disp, const V4Data& data, long now);
