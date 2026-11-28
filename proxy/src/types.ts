export interface Env {
  DB: D1Database;
  ANTHROPIC_API_KEY: string;
  PROXY_SHARED_SECRET: string;
  // v3: monthly spend cap for the budget bar ([vars] in wrangler.toml).
  MONTHLY_LIMIT_USD?: string;
}

export interface RateLimitData {
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

export interface UsageRow {
  ts: number;
  model: string;
  input_tokens: number;
  output_tokens: number;
  cache_read_tokens: number;
  cache_creation_tokens: number;
  cost_usd: number;
}

export interface AnthropicUsage {
  input_tokens?: number;
  output_tokens?: number;
  cache_read_input_tokens?: number;
  cache_creation_input_tokens?: number;
}

export interface StatsResponse {
  t: number;
  in: number;
  out: number;
  cr: number;
  cc: number;
  usd: number;
  spark: number[];
}
