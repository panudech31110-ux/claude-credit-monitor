#pragma once
#include "display_st7305.h"

// v4 mascot: a 10×10-cell pixel creature (squarish body, two eye cutouts, a
// mouth cutout, two feet), drawn at 10 px per cell → 100×100 px.
// Grid matches the "Mascot states" section of the v4 design preview.
// Animated with hops, blinks and tilts (shear) — never free rotation.

static constexpr int MASCOT_CELLS   = 10;
static constexpr int MASCOT_CELL_PX = 10;
static constexpr int MASCOT_SIZE    = MASCOT_CELLS * MASCOT_CELL_PX;  // 100

enum class MascotState : uint8_t {
  Sleeping,   // bridge offline, or every card < 5%   → lying on its side + zZ
  Idle,       // every card < 25%                     → occasional blink
  Active,     // any card 25–60%                      → small hop every 3 s
  Busy,       // any card 60–80%                      → bigger hop every 1 s
  Critical,   // any card >= 80%                      → inverted + jump every 0.5 s
  Worried,    // any card >= 100%                     → head-tilt wobble every 0.5 s
};
static constexpr int MASCOT_STATE_COUNT = 6;

const char* mascotStateName(MascotState s);

// Picks the state from the highest card percentage (ratio, 1.0 = 100%).
MascotState mascotStateFor(bool online, float maxPct);

// Everything that changes what's on screen. Equal frames → no redraw.
struct MascotFrame {
  MascotState state = MascotState::Idle;
  int8_t dy    = 0;      // hop offset in px (negative = up)
  int8_t tilt  = 0;      // -1 lean left, 0 upright, +1 lean right
  bool   blink = false;  // eyes closed
  bool   zz    = false;  // sleeping: second "Z" visible
  bool operator==(const MascotFrame& o) const {
    return state == o.state && dy == o.dy && tilt == o.tilt && blink == o.blink && zz == o.zz;
  }
  bool operator!=(const MascotFrame& o) const { return !(*this == o); }
};

// Frame for state `s` at time `ms` (millis()).
MascotFrame mascotFrameAt(MascotState s, uint32_t ms);

// Draws the creature for frame `f` with its resting top-left at (x, y); the
// hop offset is applied here. `on` = ink colour (false for the inverted state).
// Only sets pixels inside the silhouette.
void drawMascot(DisplayST7305& disp, int x, int y, const MascotFrame& f, bool on);
