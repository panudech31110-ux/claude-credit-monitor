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
static const int EYE_ROW = 3, EYE_COL_L = 2, EYE_COL_R = 7;

const char* mascotStateName(MascotState s) {
  switch (s) {
    case MascotState::Sleeping: return "sleeping";
    case MascotState::Idle:     return "idle";
    case MascotState::Active:   return "active";
    case MascotState::Busy:     return "busy";
    case MascotState::Critical: return "critical";
    case MascotState::Worried:  return "worried";
  }
  return "?";
}

MascotState mascotStateFor(bool online, float maxPct) {
  if (!online || maxPct < 0.05f) return MascotState::Sleeping;
  if (maxPct >= 1.00f) return MascotState::Worried;
  if (maxPct >= 0.80f) return MascotState::Critical;
  if (maxPct >= 0.60f) return MascotState::Busy;
  if (maxPct >= 0.25f) return MascotState::Active;
  return MascotState::Idle;
}

// Timing per state (spec table, adapted to hops/tilts):
//   sleeping  second "Z" toggles every 1.5 s
//   idle      blink 200 ms every 4 s
//   active    hop 4 px for 300 ms every 3 s
//   busy      hop 6 px for 250 ms every 1 s
//   critical  jump 8 px for 250 ms every 0.5 s (drawn inverted)
//   worried   lean left/right, switching every 0.5 s
MascotFrame mascotFrameAt(MascotState s, uint32_t ms) {
  MascotFrame f;
  f.state = s;
  switch (s) {
    case MascotState::Sleeping: f.zz = (ms / 1500) % 2;                   break;
    case MascotState::Idle:     f.blink = (ms % 4000) < 200;              break;
    case MascotState::Active:   f.dy = (ms % 3000) < 300 ? -4 : 0;        break;
    case MascotState::Busy:     f.dy = (ms % 1000) < 250 ? -6 : 0;        break;
    case MascotState::Critical: f.dy = (ms % 500)  < 250 ? -8 : 0;        break;
    case MascotState::Worried:  f.tilt = ((ms / 500) % 2) ? 1 : -1;       break;
  }
  return f;
}

static bool idleCell(int r, int c) {
  return (IDLE_ROWS[r] >> (MASCOT_CELLS - 1 - c)) & 1;
}

static bool isEye(int r, int c) {
  return r == EYE_ROW && (c == EYE_COL_L || c == EYE_COL_R);
}

// Tilt is a shear pivoting on the feet: the top row shifts 6 px, the bottom 0.
static int tiltDx(int tilt, int r) {
  return tilt * (MASCOT_CELLS - 1 - r) * 2 / 3;
}

void drawMascot(DisplayST7305& d, int x, int y, const MascotFrame& f, bool on) {
  const int P = MASCOT_CELL_PX;
  y += f.dy;

  if (f.state == MascotState::Sleeping) {
    // Lying on its side (grid turned a quarter left: head left, feet right),
    // eyes shut. new[r][c] = old[c][9 - r].
    for (int r = 0; r < MASCOT_CELLS; r++)
      for (int c = 0; c < MASCOT_CELLS; c++) {
        const int orow = c, ocol = MASCOT_CELLS - 1 - r;
        if (idleCell(orow, ocol) || isEye(orow, ocol))
          d.fillRect(x + c * P, y + r * P, P, P, on);
      }
    // Closed-eye slits, turned with the body (vertical). Old eye (3,2) → (7,3),
    // old eye (3,7) → (2,3).
    d.fillRect(x + 3 * P + 4, y + 7 * P, 2, P, !on);
    d.fillRect(x + 3 * P + 4, y + 2 * P, 2, P, !on);
    return;
  }

  for (int r = 0; r < MASCOT_CELLS; r++) {
    const int dx = tiltDx(f.tilt, r);
    for (int c = 0; c < MASCOT_CELLS; c++) {
      if (idleCell(r, c) || (f.blink && isEye(r, c)))
        d.fillRect(x + dx + c * P, y + r * P, P, P, on);
    }
  }
  if (f.blink) {
    // Closed eyes: a 2 px slit across each eye cell.
    const int dx = tiltDx(f.tilt, EYE_ROW);
    d.fillRect(x + dx + EYE_COL_L * P, y + EYE_ROW * P + 4, P, 2, !on);
    d.fillRect(x + dx + EYE_COL_R * P, y + EYE_ROW * P + 4, P, 2, !on);
  }
}
