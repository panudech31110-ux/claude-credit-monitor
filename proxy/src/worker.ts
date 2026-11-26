import type { Env, AnthropicUsage, RateLimitData } from "./types";
import { handleStats } from "./stats";
import { runScheduled } from "./scheduled";
import { getPricing, calcCost } from "./pricing";

const ANTHROPIC_BASE = "https://api.anthropic.com";
const DEFAULT_VERSION = "2023-06-01";

export default {
  async fetch(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
    // Accept X-Proxy-Auth (firmware/curl) or x-api-key (Anthropic SDK / Claude Code)
    const secret = request.headers.get("X-Proxy-Auth") ?? request.headers.get("x-api-key");
    if (!secret || secret !== env.PROXY_SHARED_SECRET) {
      return new Response("Unauthorized", { status: 401 });
    }

    const url = new URL(request.url);

    if (url.pathname === "/stats") {
      return handleStats(request, env);
    }

    return proxyRequest(request, env, ctx, url);
  },

  // v3: cron trigger (every 15 min) — pull real org spend from the Admin API.
  // Isolated from the /v1/messages pass-through; failures here never affect proxying.
  async scheduled(_controller: ScheduledController, env: Env, ctx: ExecutionContext): Promise<void> {
    ctx.waitUntil(
      runScheduled(env).catch((e) => console.warn("scheduled handler error:", e)),
    );
  },
};

async function proxyRequest(
  request: Request,
  env: Env,
  ctx: ExecutionContext,
  url: URL,
): Promise<Response> {
  const outHeaders = new Headers(request.headers);
  outHeaders.delete("X-Proxy-Auth");
  outHeaders.set("x-api-key", env.ANTHROPIC_API_KEY);
  if (!outHeaders.has("anthropic-version")) {
    outHeaders.set("anthropic-version", DEFAULT_VERSION);
  }
  outHeaders.set("host", "api.anthropic.com");

  const bodyText =
    request.method !== "GET" && request.method !== "HEAD"
      ? await request.text()
      : null;

  let isStreaming = false;
  if (bodyText) {
    try {
      isStreaming = JSON.parse(bodyText).stream === true;
    } catch {}
  }

  const upstream = await fetch(
    `${ANTHROPIC_BASE}${url.pathname}${url.search}`,
    { method: request.method, headers: outHeaders, body: bodyText ?? undefined },
  );

  if (!upstream.ok) {
    return upstream;
  }

  const rl = parseRateLimitHeaders(upstream.headers);

  if (isStreaming) {
    const [clientStream, logStream] = upstream.body!.tee();
    ctx.waitUntil(consumeStreamAndLog(logStream, env, rl));
    return new Response(clientStream, {
      status: upstream.status,
      headers: upstream.headers,
    });
  }

  // Non-streaming: buffer → log → return
  const text = await upstream.text();
  try {
    const json = JSON.parse(text);
    await logUsage(env, json.model ?? "unknown", json.usage ?? {}, rl);
  } catch (e) {
    console.error("usage log failed:", e);
  }

  return new Response(text, {
    status: upstream.status,
    headers: upstream.headers,
  });
}

async function consumeStreamAndLog(
  stream: ReadableStream<Uint8Array>,
  env: Env,
  rl?: RateLimitData,
): Promise<void> {
  const reader = stream.getReader();
  const decoder = new TextDecoder();
  let buffer = "";
  let model = "unknown";
  let inputTokens = 0;
  let outputTokens = 0;
  let cacheRead = 0;
  let cacheCreation = 0;

  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });

      const lines = buffer.split("\n");
      buffer = lines.pop() ?? "";

      for (const line of lines) {
        if (!line.startsWith("data: ")) continue;
        const data = line.slice(6).trim();
        if (data === "[DONE]") continue;
        try {
          const event = JSON.parse(data);
          if (event.type === "message_start") {
            model = event.message?.model ?? "unknown";
            const u = event.message?.usage ?? {};
            inputTokens = u.input_tokens ?? 0;
            cacheRead = u.cache_read_input_tokens ?? 0;
            cacheCreation = u.cache_creation_input_tokens ?? 0;
          } else if (event.type === "message_delta") {
            // message_delta carries the final output_tokens count
            outputTokens = event.usage?.output_tokens ?? outputTokens;
          }
        } catch {}
      }
    }
  } finally {
    reader.releaseLock();
  }

  await logUsage(env, model, {
    input_tokens: inputTokens,
    output_tokens: outputTokens,
    cache_read_input_tokens: cacheRead,
    cache_creation_input_tokens: cacheCreation,
  }, rl);
}

function parseRateLimitHeaders(headers: Headers): RateLimitData {
  const intH = (name: string) => {
    const v = headers.get(name);
    return v !== null ? parseInt(v, 10) : null;
  };
  const tsH = (name: string) => {
    const v = headers.get(name);
    if (v === null) return null;
    const ms = Date.parse(v);
    return isNaN(ms) ? null : Math.floor(ms / 1000);
  };
  return {
    rpm_limit:      intH("anthropic-ratelimit-requests-limit"),
    rpm_remaining:  intH("anthropic-ratelimit-requests-remaining"),
    rpm_reset:      tsH("anthropic-ratelimit-requests-reset"),
    itpm_limit:     intH("anthropic-ratelimit-input-tokens-limit"),
    itpm_remaining: intH("anthropic-ratelimit-input-tokens-remaining"),
    itpm_reset:     tsH("anthropic-ratelimit-input-tokens-reset"),
    otpm_limit:     intH("anthropic-ratelimit-output-tokens-limit"),
    otpm_remaining: intH("anthropic-ratelimit-output-tokens-remaining"),
    otpm_reset:     tsH("anthropic-ratelimit-output-tokens-reset"),
  };
}

async function saveRateLimits(env: Env, rl: RateLimitData): Promise<void> {
  // Only update fields that are non-null (keep previous values for missing headers)
  const fields: string[] = ["updated_at = ?"];
  const vals: (number | null)[] = [Math.floor(Date.now() / 1000)];
  const cols: Array<keyof RateLimitData> = [
    "rpm_limit", "rpm_remaining", "rpm_reset",
    "itpm_limit", "itpm_remaining", "itpm_reset",
    "otpm_limit", "otpm_remaining", "otpm_reset",
  ];
  for (const col of cols) {
    if (rl[col] !== null) {
      fields.push(`${col} = ?`);
      vals.push(rl[col] as number);
    }
  }
  vals.push(1); // WHERE id = 1
  await env.DB.prepare(
    `UPDATE ratelimits SET ${fields.join(", ")} WHERE id = 1`
  ).bind(...vals).run();
}

async function logUsage(
  env: Env,
  model: string,
  usage: AnthropicUsage,
  rl?: RateLimitData,
): Promise<void> {
  const input = usage.input_tokens ?? 0;
  const output = usage.output_tokens ?? 0;
  const cacheRead = usage.cache_read_input_tokens ?? 0;
  const cacheCreation = usage.cache_creation_input_tokens ?? 0;

  const pricing = getPricing(model);
  const cost = pricing
    ? calcCost(pricing, input, output, cacheRead, cacheCreation)
    : 0;

  const saves: Promise<unknown>[] = [
    env.DB.prepare(
      `INSERT INTO usage
         (ts, model, input_tokens, output_tokens, cache_read_tokens, cache_creation_tokens, cost_usd)
       VALUES (?, ?, ?, ?, ?, ?, ?)`,
    ).bind(Math.floor(Date.now() / 1000), model, input, output, cacheRead, cacheCreation, cost).run(),
  ];
  if (rl) saves.push(saveRateLimits(env, rl));
  await Promise.all(saves);
}
