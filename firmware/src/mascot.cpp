#include "mascot.h"

// One row per cell row; bit 9 = leftmost column, bit 0 = rightmost.
//   row 0  ..XXXXXX..   top of head (rounded corners)
//   row 1  .XXXXXXXX.
//   row 2  XXXXXXXXXX
//   row 3  XX.XXXX.XX   eye cutouts at cols 2 and 7
//   row 4  XXXXXXXXXX
//   row 5  XXXXXXXXXX
//   row 6  XXXX..XXXX   mouth cutout at cols 4-5
//   row 7  XXXXXXXXXX
//   row 8  XXXXXXXXXX
//   row 9  .XX....XX.   two feet
static const uint16_t IDLE_ROWS[MASCOT_CELLS] = {
  0b0011111100,
  0b0111111110,
  0b1111111111,
  0b1101111011,
  0b1111111111,
  0b1111111111,
  0b1111001111,
  0b1111111111,
  0b1111111111,
  0b0110000110,
};

static void drawGrid(DisplayST7305& disp, int x, int y, const uint16_t* rows) {
  for (int r = 0; r < MASCOT_CELLS; r++) {
    for (int c = 0; c < MASCOT_CELLS; c++) {
      if (rows[r] & (1u << (MASCOT_CELLS - 1 - c))) {
        disp.fillRect(x + c * MASCOT_CELL_PX, y + r * MASCOT_CELL_PX,
                      MASCOT_CELL_PX, MASCOT_CELL_PX, true);
      }
    }
  }
}

void drawMascotIdle(DisplayST7305& disp, int x, int y) {
  drawGrid(disp, x, y, IDLE_ROWS);
}
