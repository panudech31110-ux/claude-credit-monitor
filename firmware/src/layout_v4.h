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
  bool        reset_clock = false;  // true → "Resets at HH:MM" (local 24h), false → countdown
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

// Redraws the whole v4 view and pushes the full frame (first draw / view switch).
void v4RenderFull(DisplayST7305& disp, const V4Data& data, long now, uint32_t ms);

// Redraws + pushes only the card column, and only if what it would show changed
// (values, bar, or a countdown minute). Also updates the mascot's state from the
// data. Returns true if it redrew.
bool v4UpdateCards(DisplayST7305& disp, const V4Data& data, long now);

// Call often (every loop tick). Redraws + pushes only the mascot column, and only
// when the animation frame changed. Returns true if it redrew.
bool v4TickMascot(DisplayST7305& disp, uint32_t ms);

// Test override: 0..5 = sleeping, idle, active, busy, critical, worried; -1 = auto.
void v4ForceMascotState(int state);
const char* v4MascotStateName();
