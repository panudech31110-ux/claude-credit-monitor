import type { Env } from "./types";

// v4: Claude Code usage pushed by the laptop bridge (bridge/bridge.py, via ccusage).

const WINDOWS = ["session", "today", "week"] as const;
type WindowName = (typeof WINDOWS)[number];

export interface CcWindow {
  cost: number;
  pct: number;
  reset_at: number;
  tokens: number;
}

export type CcPayload = Record<WindowName, CcWindow>;

// Data older than this is hidden from /stats (firmware shows "bridge offline").
export const CC_STALE_SECONDS = 15 * 60;

interface CcRow {
  updated_at: number;
  session_cost: number | null; session_pct: number | null; session_reset_at: number | null; session_tokens: number | null;
  today_cost: number | null;   today_pct: number | null;   today_reset_at: number | null;   today_tokens: number | null;
  week_cost: number | null;    week_pct: number | null;    week_reset_at: number | null;    week_tokens: number | null;
}

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

function isNonNegNumber(v: unknown): v is number {
  return typeof v === "number" && Number.isFinite(v) && v >= 0;
}

// Returns the validated payload, or an error message naming the bad field.
function validate(body: unknown): CcPayload | string {
  if (typeof body !== "object" || body === null) return "body must be a JSON object";
  const out = {} as CcPayload;
  for (const name of WINDOWS) {
    const w = (body as Record<string, unknown>)[name];
    if (typeof w !== "object" || w === null) return `${name}: missing or not an object`;
    const { cost, pct, reset_at, tokens } = w as Record<string, unknown>;
    if (!isNonNegNumber(cost)) return `${name}.cost: expected number >= 0`;
    if (!isNonNegNumber(pct)) return `${name}.pct: expected number >= 0`;
    if (!isNonNegNumber(reset_at) || !Number.isInteger(reset_at)) return `${name}.reset_at: expected unix seconds`;
    if (!isNonNegNumber(tokens) || !Number.isInteger(tokens)) return `${name}.tokens: expected integer >= 0`;
    out[name] = { cost, pct, reset_at, tokens };
  }
  return out;
}

export async function handleIngestCc(request: Request, env: Env): Promise<Response> {
  if (request.method !== "POST") {
    return json({ ok: false, error: "method not allowed" }, 405);
  }
  const auth = request.headers.get("X-Ingest-Auth");
  if (!env.INGEST_SECRET || !auth || auth !== env.INGEST_SECRET) {
    return new Response("Unauthorized", { status: 401 });
  }

  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return json({ ok: false, error: "invalid JSON" }, 400);
  }
  const p = validate(body);
  if (typeof p === "string") {
    return json({ ok: false, error: p }, 400);
  }

  // Singleton row is created by schema_v4.sql; upsert anyway so a fresh DB works.
  await env.DB.prepare(`
    INSERT INTO cc_usage (
      id, updated_at,
      session_cost, session_pct, session_reset_at, session_tokens,
      today_cost,   today_pct,   today_reset_at,   today_tokens,
      week_cost,    week_pct,    week_reset_at,    week_tokens
    ) VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
      updated_at = excluded.updated_at,
      session_cost = excluded.session_cost, session_pct = excluded.session_pct,
      session_reset_at = excluded.session_reset_at, session_tokens = excluded.session_tokens,
      today_cost = excluded.today_cost, today_pct = excluded.today_pct,
      today_reset_at = excluded.today_reset_at, today_tokens = excluded.today_tokens,
      week_cost = excluded.week_cost, week_pct = excluded.week_pct,
      week_reset_at = excluded.week_reset_at, week_tokens = excluded.week_tokens
  `).bind(
    Math.floor(Date.now() / 1000),
    p.session.cost, p.session.pct, p.session.reset_at, p.session.tokens,
    p.today.cost,   p.today.pct,   p.today.reset_at,   p.today.tokens,
    p.week.cost,    p.week.pct,    p.week.reset_at,    p.week.tokens,
  ).run();

  return json({ ok: true });
}

// Builds the /stats "cc" block, or null when never ingested or stale.
export async function loadCcBlock(env: Env): Promise<Record<string, unknown> | null> {
  const row = await env.DB.prepare(`SELECT * FROM cc_usage WHERE id = 1`).first<CcRow>();
  const now = Math.floor(Date.now() / 1000);
  if (!row || row.updated_at <= 0 || now - row.updated_at >= CC_STALE_SECONDS) {
    return null;
  }
  const r4 = (n: number | null) => Math.round((n ?? 0) * 1e4) / 1e4;
  return {
    updated: row.updated_at,
    session: { pct: r4(row.session_pct), cost: r4(row.session_cost), reset_at: row.session_reset_at ?? 0 },
    today:   { pct: r4(row.today_pct),   cost: r4(row.today_cost),   reset_at: row.today_reset_at ?? 0 },
    week:    { pct: r4(row.week_pct),    cost: r4(row.week_cost),    reset_at: row.week_reset_at ?? 0 },
  };
}
