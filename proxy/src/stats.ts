import type { Env } from "./types";

interface TotalsRow {
  in_: number;
  out_: number;
  cr: number;
  cc: number;
  usd: number;
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
  const [totals, day, week, buckets, rlRow] = await Promise.all([
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

  return new Response(JSON.stringify(body), {
    headers: { "Content-Type": "application/json" },
  });
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
