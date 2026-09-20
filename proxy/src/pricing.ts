// Prices fetched from https://www.anthropic.com/pricing on 2026-09-14 (USD per 1M tokens).
// More-specific prefixes MUST come before shorter ones (e.g. "claude-fable-5-1" before "claude-fable-5").
export interface ModelPricing {
  input: number;
  output: number;
  cache_read: number;
  cache_creation: number;
}

export const PRICING: Record<string, ModelPricing> = {
  // ── Current generation ────────────────────────────────────────────────────
  "claude-fable-5-1":  { input: 10,  output: 50,  cache_read: 0.25, cache_creation: 12.50 },
  "claude-opus-5":     { input:  5,  output: 25,  cache_read: 0.50, cache_creation:  6.25 },
  "claude-sonnet-5":   { input:  2,  output: 10,  cache_read: 0.20, cache_creation:  2.50 },
  "claude-haiku-4-5":  { input:  1,  output:  5,  cache_read: 0.10, cache_creation:  1.25 },

  // ── Legacy / previous releases ────────────────────────────────────────────
  "claude-fable-5":    { input: 10,  output: 50,  cache_read: 1.00, cache_creation: 12.50 },
  "claude-opus-4-8":   { input:  5,  output: 25,  cache_read: 0.50, cache_creation:  6.25 },
  "claude-sonnet-4-6": { input:  3,  output: 15,  cache_read: 0.30, cache_creation:  3.75 },
  "claude-opus-4-7":   { input:  5,  output: 25,  cache_read: 0.50, cache_creation:  6.25 },
  "claude-opus-4-6":   { input:  5,  output: 25,  cache_read: 0.50, cache_creation:  6.25 },
  "claude-sonnet-4-5": { input:  3,  output: 15,  cache_read: 0.30, cache_creation:  3.75 },
  "claude-opus-4-5":   { input:  5,  output: 25,  cache_read: 0.50, cache_creation:  6.25 },
  "claude-opus-4-1":   { input: 15,  output: 75,  cache_read: 1.50, cache_creation: 18.75 },
};

export function getPricing(model: string): ModelPricing | null {
  for (const prefix of Object.keys(PRICING)) {
    if (model.startsWith(prefix)) return PRICING[prefix];
  }
  console.warn(`Unknown model for pricing: ${model}`);
  return null;
}

export function calcCost(
  pricing: ModelPricing,
  input: number,
  output: number,
  cacheRead: number,
  cacheCreation: number,
): number {
  const M = 1_000_000;
  return (
    (input * pricing.input +
      output * pricing.output +
      cacheRead * pricing.cache_read +
      cacheCreation * pricing.cache_creation) /
    M
  );
}
