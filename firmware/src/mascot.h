#pragma once
#include "display_st7305.h"

// v4 mascot: a 10×10-cell pixel creature (squarish body, two eye cutouts, a
// mouth cutout, two feet), drawn at 10 px per cell → 100×100 px.
// Grid matches the "Mascot states" section of the v4 design preview.

static constexpr int MASCOT_CELLS  = 10;
static constexpr int MASCOT_CELL_PX = 10;
static constexpr int MASCOT_SIZE   = MASCOT_CELLS * MASCOT_CELL_PX;  // 100

// Draws the idle creature with its top-left corner at (x, y). Pixels outside
// the silhouette (and the eye/mouth cutouts) are left untouched.
void drawMascotIdle(DisplayST7305& disp, int x, int y);
