// v3: scheduled (cron) handler — pulls real month-to-date spend from Anthropic's
// Admin API every 15 min and upserts the org_spend singleton. Never throws; on any
// failure it keeps the last-known values so the firmware keeps showing good data.
//
// NOTE: the exact Admin API response shapes for spend_limits and credit balance are
// verified in build step 4. Until then cost_report (the primary number) is wired for
// real, and limits/credit degrade gracefully.
import type { Env, OrgSpendRow } from "./types";

const ADMIN_BASE = "https://api.anthropic.com";
const ADMIN_VERSION = "2023-06-01";

interface AdminResult {
  ok: boolean;
  status: number;
  json: any;
  text?: string;
}

async function adminGet(env: Env, url: string): Promise<AdminResult> {
  const r = await fetch(url, {
    headers: {
      "x-api-key": env.ADMIN_API_KEY,
      "anthropic-version": ADMIN_VERSION,
    },
  });
  if (!r.ok) {
    const text = await r.text().catch(() => "");
    return { ok: false, status: r.status, json: null, text };
  }
  const json = await r.json().catch(() => null);
  return { ok: true, status: r.status, json };
}

// Sum every USD cost bucket the cost_report returns. Defensive about shape:
// amount may be a string ("0.1234") or number, nested under data[].results[].
function sumCostReport(json: any): number {
  let total = 0;
  const buckets = Array.isArray(json?.data) ? json.data : [];
  for (const b of buckets) {
    const results = Array.isArray(b?.results) ? b.results : [];
    for (const res of results) {
      const raw = res?.amount ?? res?.cost ?? 0;
      const amt = typeof raw === "string" ? parseFloat(raw) : raw;
      if (typeof amt === "number" && !isNaN(amt)) total += amt;
    }
  }
  return Math.round(total * 1e6) / 1e6;
}

export async function runScheduled(env: Env): Promise<void> {
  // UTC month start — both for the cost_report query and the stored month_start_ts.
  const now = new Date();
  const monthStart = new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), 1));
  const monthStartIso = monthStart.toISOString();
  const monthStartTs = Math.floor(monthStart.getTime() / 1000);

  // Default to last-known values so a failed fetch never zeroes anything out.
  const existing = await env.DB
    .prepare(`SELECT * FROM org_spend WHERE id = 1`)
    .first<OrgSpendRow>();

  let monthUsd = existing?.month_usd ?? 0;
  let monthLimit = existing?.month_limit_usd ?? 0;
  const credit = existing?.credit_balance_usd ?? 0; // endpoint TBD — wired in step 4

  // ── 1. Month-to-date cost (primary number) ──────────────────────────────────
  let costOk = false;
  try {
    const url =
      `${ADMIN_BASE}/v1/organizations/cost_report` +
      `?starting_at=${encodeURIComponent(monthStartIso)}&bucket_width=1d`;
    const res = await adminGet(env, url);
    if (res.ok) {
      monthUsd = sumCostReport(res.json);
      costOk = true;
    } else {
      console.warn(`cost_report HTTP ${res.status}: ${(res.text ?? "").slice(0, 200)}`);
    }
  } catch (e) {
    console.warn("cost_report fetch failed:", (e as Error).message);
  }

  // If the primary fetch failed, leave the row untouched (don't bump updated_at) so
  // the firmware keeps trusting the last good snapshot.
  if (!costOk) {
    console.warn("scheduled: cost_report unavailable — keeping last-known org_spend");
    return;
  }

  // ── 2. Monthly spend limit — from config var ────────────────────────────────
  // Verified in step 4: the Spend Limits API is Claude Enterprise-only and not
  // available to Console organizations, so there's no endpoint to read the cap.
  // The tier cap (e.g. $500 on Start) lives only on the Console Rate-limits page.
  // Source it from the MONTHLY_LIMIT_USD config var; keep last-known if unset.
  if (env.MONTHLY_LIMIT_USD) {
    const envLim = parseFloat(env.MONTHLY_LIMIT_USD);
    if (!isNaN(envLim)) monthLimit = envLim;
  }

  // ── 3. Credit balance — no Admin API endpoint exists (verified step 4). ──────
  // Left at 0; the firmware hides this field. See README for details.

  // ── Upsert singleton ─────────────────────────────────────────────────────────
  await env.DB.prepare(
    `UPDATE org_spend
       SET updated_at = ?, month_usd = ?, month_limit_usd = ?, credit_balance_usd = ?, month_start_ts = ?
     WHERE id = 1`,
  )
    .bind(Math.floor(Date.now() / 1000), monthUsd, monthLimit, credit, monthStartTs)
    .run();

  console.log(
    `org_spend updated: month_usd=${monthUsd.toFixed(4)} limit=${monthLimit} credit=${credit}`,
  );
}
