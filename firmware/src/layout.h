#pragma once
#include "display_st7305.h"

struct PeriodData {
  long  in_tokens    = 0;
  long  out_tokens   = 0;
  long  cache_read   = 0;
  long  cache_creation = 0;
  float cost_usd     = 0.0f;
};

struct RlDim {
  long lim = 0;
  long rem = 0;
};

struct StatsData {
  // 24h rolling (existing)
  long  in_tokens    = 0;
  long  out_tokens   = 0;
  long  cache_read   = 0;
  long  cache_creation = 0;
  float cost_usd     = 0.0f;
  int   spark[7]     = {};

  // v2: daily (calendar day, Bangkok UTC+7) and weekly (7-day rolling)
  PeriodData day;
  PeriodData week;

  // v2: rate-limit dims (present only if rl field arrived)
  bool  rl_valid     = false;
  RlDim rl_rpm;
  RlDim rl_itpm;
  RlDim rl_otpm;

  int   today_dow    = 0;   // tm_wday: 0=Sun 1=Mon … 6=Sat
  char  timestamp[6] = "??:??";
  bool  fetch_error  = false;
};

void renderLayout(DisplayST7305& disp, const StatsData& data);
