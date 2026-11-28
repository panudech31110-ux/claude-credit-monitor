import type { Env } from "./types";
import { loadCcBlock } from "./ingest";

const DEFAULT_MONTHLY_LIMIT_USD = 500;

interface TotalsRow {
  in_: number;
  out_: number;
  cr: number;
  cc: number;
  usd: number;
}

interface MonthRow {
  usd: number;
  tok: number;
}

interface BucketRow {
  d: string;
  total: number;
}

interface RlRow {
  updated_at: number;
  rpm_limit: number | null;
  rpm_remaining: number | null;
  rpm_reset: number | null;
  itpm_limit: number | null;
  itpm_remaining: number | null;
  itpm_reset: number | null;
  otpm_limit: number | null;
  otpm_remaining: number | null;
  otpm_reset: number | null;
}

export async function handleStats(
  _request: Request,
  env: Env,
): Promise<Response> {
  const [totals, day, week, buckets, rlRow, monthRow, ccBlock] = await Promise.all([
    env.DB.prepare(`
      SELECT
        COALESCE(SUM(input_tokens),0)          AS in_,
        COALESCE(SUM(output_tokens),0)         AS out_,
        COALESCE(SUM(cache_read_tokens),0)     AS cr,
        COALESCE(SUM(cache_creation_tokens),0) AS cc,
        COALESCE(SUM(cost_usd),0)              AS usd
      FROM usage WHERE ts >= strftime('%s','now','-1 day')
    `).first<TotalsRow>(),

    env.DB.prepare(`
      SELECT
        COALESCE(SUM(input_tokens),0)          AS in_,
        COALESCE(SUM(output_tokens),0)         AS out_,
        COALESCE(SUM(cache_read_tokens),0)     AS cr,
        COALESCE(SUM(cache_creation_tokens),0) AS cc,
        COALESCE(SUM(cost_usd),0)              AS usd
      FROM usage
      WHERE ts >= strftime('%s','now','start of day','-7 hours')
    `).first<TotalsRow>(),

    env.DB.prepare(`
      SELECT
        COALESCE(SUM(input_tokens),0)          AS in_,
        COALESCE(SUM(output_tokens),0)         AS out_,
        COALESCE(SUM(cache_read_tokens),0)     AS cr,
        COALESCE(SUM(cache_creation_tokens),0) AS cc,
        COALESCE(SUM(cost_usd),0)              AS usd
      FROM usage
      WHERE ts >= strftime('%s','now','-7 days')
    `).first<TotalsRow>(),

    env.DB.prepare(`
      SELECT date(ts,'unixepoch') AS d,
             COALESCE(SUM(input_tokens+output_tokens),0) AS total
      FROM usage
      WHERE ts >= strftime('%s','now','-7 days')
      GROUP BY d ORDER BY d
    `).all<BucketRow>(),

    env.DB.prepare(`SELECT * FROM ratelimits WHERE id = 1`).first<RlRow>(),

    // v3: month-to-date from our own usage log (calendar month, Bangkok UTC+7).
    env.DB.prepare(`
      SELECT
        COALESCE(SUM(cost_usd),0)                AS usd,
        COALESCE(SUM(input_tokens+output_tokens),0) AS tok
      FROM usage
      WHERE ts >= strftime('%s','now','+7 hours','start of month','-7 hours')
    `).first<MonthRow>(),

    // v4: Claude Code usage from the bridge; null when never ingested or stale.
    loadCcBlock(env),
  ]);

  const body: Record<string, unknown> = {
    t:   Math.floor(Date.now() / 1000),
    in:  totals?.in_ ?? 0,
    out: totals?.out_ ?? 0,
    cr:  totals?.cr ?? 0,
    cc:  totals?.cc ?? 0,
    usd: Math.round((totals?.usd ?? 0) * 1e6) / 1e6,
    spark: buildSparkline(buckets.results ?? []),
    day: {
      in:  day?.in_ ?? 0,
      out: day?.out_ ?? 0,
      cr:  day?.cr ?? 0,
      cc:  day?.cc ?? 0,
      usd: Math.round((day?.usd ?? 0) * 1e6) / 1e6,
    },
    week: {
      in:  week?.in_ ?? 0,
      out: week?.out_ ?? 0,
      cr:  week?.cr ?? 0,
      cc:  week?.cc ?? 0,
      usd: Math.round((week?.usd ?? 0) * 1e6) / 1e6,
    },
  };

  if (rlRow && rlRow.updated_at > 0) {
    body.rl = {
      rpm:  { lim: rlRow.rpm_limit,  rem: rlRow.rpm_remaining,  rst: rlRow.rpm_reset },
      itpm: { lim: rlRow.itpm_limit, rem: rlRow.itpm_remaining, rst: rlRow.itpm_reset },
      otpm: { lim: rlRow.otpm_limit, rem: rlRow.otpm_remaining, rst: rlRow.otpm_reset },
    };
  }

  // v3: month block — computed from our own usage log (the Admin API cost_report
  // isn't enabled on this account). Always present, so the firmware shows v3.
  {
    const monthLimit = parseMonthlyLimit(env);
    body.month = buildMonth(
      Math.round((monthRow?.usd ?? 0) * 1e6) / 1e6,
      monthRow?.tok ?? 0,
      monthLimit,
    );
  }

  // v4: absent → firmware shows "bridge offline".
  if (ccBlock) body.cc = ccBlock;

  return new Response(JSON.stringify(body), {
    headers: { "Content-Type": "application/json" },
  });
}

// Monthly cap from the MONTHLY_LIMIT_USD config var ([vars] in wrangler.toml),
// falling back to a sane default. (Anthropic's Spend Limits API is Enterprise-only.)
function parseMonthlyLimit(env: Env): number {
  if (env.MONTHLY_LIMIT_USD) {
    const n = parseFloat(env.MONTHLY_LIMIT_USD);
    if (!isNaN(n) && n > 0) return n;
  }
  return DEFAULT_MONTHLY_LIMIT_USD;
}

// v3: build the month block with burn-rate projection. monthUsd is month-to-date
// spend summed from the usage log. All date math is in Bangkok (UTC+7) to match
// the firmware clock and the "Day N / M" header.
function buildMonth(
  monthUsd: number,
  monthTok: number,
  monthLimit: number,
): Record<string, unknown> {
  // Shift epoch by +7h, then read with UTC getters → Bangkok wall-clock.
  const bkk = new Date(Date.now() + 7 * 3600 * 1000);
  const year = bkk.getUTCFullYear();
  const month0 = bkk.getUTCMonth(); // 0-based
  const dayOfMonth = bkk.getUTCDate();
  // Day 0 of next month = last day of this month.
  const daysInMonth = new Date(Date.UTC(year, month0 + 1, 0)).getUTCDate();
  const hoursIntoDay =
    bkk.getUTCHours() + bkk.getUTCMinutes() / 60 + bkk.getUTCSeconds() / 3600;

  // Avoid divide-by-zero on day 1 via max(…, 0.5).
  const daysElapsed = dayOfMonth - 1 + hoursIntoDay / 24;
  const burnPerDay = monthUsd / Math.max(daysElapsed, 0.5);
  const projectedEomUsd = burnPerDay * daysInMonth;
  const projectedEomPct = monthLimit > 0 ? projectedEomUsd / monthLimit : 0;

  const r6 = (n: number) => Math.round(n * 1e6) / 1e6;
  return {
    usd: r6(monthUsd),
    tok: monthTok,
    lim: r6(monthLimit),
    credit: 0,                       // no credit-balance source → firmware hides it
    updated: Math.floor(Date.now() / 1000),
    day_of_month: dayOfMonth,
    days_in_month: daysInMonth,
    projected_eom_usd: r6(projectedEomUsd),
    projected_eom_pct: r6(projectedEomPct),
  };
}

function buildSparkline(rows: BucketRow[]): number[] {
  const map = new Map<string, number>(rows.map((r) => [r.d, r.total]));

  const now = new Date();
  const spark: number[] = [];
  for (let i = 6; i >= 0; i--) {
    const d = new Date(now);
    d.setUTCDate(d.getUTCDate() - i);
    const key = d.toISOString().slice(0, 10);
    spark.push(map.get(key) ?? 0);
  }
  return spark;
}
