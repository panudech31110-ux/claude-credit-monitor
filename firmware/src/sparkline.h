// TODO (Step 8): implement sparkline rendering
#pragma once
#include "display_st7305.h"

void drawSparkline(DisplayST7305& disp, const int* values, int count, int x, int y, int w, int h, int todayDow);
