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

// v3: real month-to-date spend from the Admin API (via proxy /stats "month" block)
struct MonthData {
  bool  valid             = false;  // true only if /stats included the "month" block
  float usd               = 0.0f;   // month-to-date spend
  float lim               = 0.0f;   // monthly cap (config var / tier cap)
  float credit            = 0.0f;   // credit balance (0 = unavailable → hidden)
  long  updated           = 0;      // unix ts of last Admin API success
  int   day_of_month      = 0;      // 1..31 (Bangkok)
  int   days_in_month     = 0;      // 28..31
  float projected_eom_usd = 0.0f;   // burn-rate projection to end of month
  float projected_eom_pct = 0.0f;   // projected_eom_usd / lim (ratio)
};

// v4: Claude Code usage from the laptop bridge (/stats "cc" block)
struct CcWindow {
  float pct      = 0.0f;   // ratio: 0.5 = 50%
  float cost     = 0.0f;   // USD
  long  reset_at = 0;      // unix seconds; 0 = unknown (e.g. no active session)
};

struct CcData {
  bool     valid   = false;  // true only if /stats included a "cc" object
  long     updated = 0;      // unix ts the Worker stored the last bridge POST
  CcWindow session;
  CcWindow today;
  CcWindow week;
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

  // v3: month block (present only if Admin API has succeeded)
  MonthData month;

  // v4: cc block (absent when the bridge is offline / data older than 15 min)
  CcData cc;

  int   today_dow    = 0;   // tm_wday: 0=Sun 1=Mon … 6=Sat
  char  timestamp[6] = "??:??";
  bool  fetch_error  = false;
};

void renderLayout(DisplayST7305& disp, const StatsData& data);
