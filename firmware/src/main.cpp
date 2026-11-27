// Step 9: full app — WiFi, NTP, polling loop, layout render.
// Requires src/config.h (copy from config.h.example and fill in credentials).
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "display_st7305.h"
#include "layout.h"
#include "config.h"

static DisplayST7305 disp;
static StatsData      lastGood;
static bool           hasData = false;

// ── Buttons ───────────────────────────────────────────────────────────────────
// Both buttons are active-low. We track the previous sampled level per button and
// fire on the HIGH→LOW edge; sampling every 100 ms in loop() is its own debounce.
static bool bootPrevHigh = true;   // idle (pull-up) = HIGH
static bool userPrevHigh = true;
static int  viewMode      = 0;     // reserved for future view cycling (no-op)

// Returns true once per press (falling edge). prevHigh holds the last sampled level.
static bool buttonPressed(int pin, bool& prevHigh) {
  bool high    = (digitalRead(pin) == HIGH);
  bool pressed = (prevHigh && !high);   // HIGH → LOW transition
  prevHigh = high;
  return pressed;
}

// ── Display helpers ───────────────────────────────────────────────────────────

static void showMessage(const char* line1, const char* line2 = nullptr) {
  disp.clear();
  disp.drawText(10, 130, line1, FONT_MD);
  if (line2) disp.drawText(10, 154, line2, FONT_MD);
  disp.refresh();
}

// ── Time helpers (UTC+7 Bangkok, set by configTime in initNTP) ───────────────

static void stampTime(char* buf6) {   // writes "hh:mm\0"
  time_t now; time(&now);
  struct tm ti; localtime_r(&now, &ti);
  snprintf(buf6, 6, "%02d:%02d", ti.tm_hour, ti.tm_min);
}

static int currentDow() {
  time_t now; time(&now);
  struct tm ti; localtime_r(&now, &ti);
  return ti.tm_wday;  // 0=Sun … 6=Sat
}

static bool ntpReady() {
  time_t now; time(&now);
  return now > 1700000000L;
}

// ── WiFi ─────────────────────────────────────────────────────────────────────

static bool connectWiFi() {
  Serial.printf("WiFi → %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const unsigned long deadline = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) delay(250);
  bool ok = (WiFi.status() == WL_CONNECTED);
  Serial.printf("WiFi %s\n", ok ? WiFi.localIP().toString().c_str() : "FAILED");
  return ok;
}

// ── NTP ───────────────────────────────────────────────────────────────────────

static void initNTP() {
  configTime(7 * 3600, 0, "pool.ntp.org", "time.cloudflare.com");
  Serial.print("NTP sync");
  for (int i = 0; i < 40 && !ntpReady(); i++) { delay(250); Serial.print('.'); }
  Serial.println(ntpReady() ? " OK" : " TIMEOUT");
}

// ── /stats fetch ──────────────────────────────────────────────────────────────

static bool fetchStats(StatsData& out) {
  WiFiClientSecure client;
  client.setInsecure();   // Cloudflare cert, no pinning (v1 hobby build)

  HTTPClient http;
  if (!http.begin(client, PROXY_URL)) { Serial.println("http.begin failed"); return false; }
  http.addHeader("X-Proxy-Auth", PROXY_SECRET);
  http.setTimeout(10000);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("HTTP %d\n", code);
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) { Serial.printf("JSON: %s\n", err.c_str()); return false; }

  out.in_tokens      = doc["in"]  | 0L;
  out.out_tokens     = doc["out"] | 0L;
  out.cache_read     = doc["cr"]  | 0L;
  out.cache_creation = doc["cc"]  | 0L;
  out.cost_usd       = doc["usd"] | 0.0f;

  JsonArray spark = doc["spark"].as<JsonArray>();
  for (int i = 0; i < 7 && i < (int)spark.size(); i++)
    out.spark[i] = spark[i] | 0;

  // v2: day
  out.day.in_tokens      = doc["day"]["in"]  | 0L;
  out.day.out_tokens     = doc["day"]["out"] | 0L;
  out.day.cache_read     = doc["day"]["cr"]  | 0L;
  out.day.cache_creation = doc["day"]["cc"]  | 0L;
  out.day.cost_usd       = doc["day"]["usd"] | 0.0f;

  // v2: week
  out.week.in_tokens      = doc["week"]["in"]  | 0L;
  out.week.out_tokens     = doc["week"]["out"] | 0L;
  out.week.cache_read     = doc["week"]["cr"]  | 0L;
  out.week.cache_creation = doc["week"]["cc"]  | 0L;
  out.week.cost_usd       = doc["week"]["usd"] | 0.0f;

  // v3: month block (optional — present only if Admin API has succeeded)
  JsonObject m = doc["month"];
  out.month.valid = !m.isNull();
  if (out.month.valid) {
    out.month.usd               = m["usd"]               | 0.0f;
    out.month.lim               = m["lim"]               | 0.0f;
    out.month.credit            = m["credit"]            | 0.0f;
    out.month.updated           = m["updated"]           | 0L;
    out.month.day_of_month      = m["day_of_month"]      | 0;
    out.month.days_in_month     = m["days_in_month"]     | 0;
    out.month.projected_eom_usd = m["projected_eom_usd"] | 0.0f;
    out.month.projected_eom_pct = m["projected_eom_pct"] | 0.0f;
  }

  // v2: rate limits (optional)
  JsonObject rl = doc["rl"];
  out.rl_valid = !rl.isNull();
  if (out.rl_valid) {
    out.rl_rpm.lim  = rl["rpm"]["lim"]  | 0L;
    out.rl_rpm.rem  = rl["rpm"]["rem"]  | 0L;
    out.rl_itpm.lim = rl["itpm"]["lim"] | 0L;
    out.rl_itpm.rem = rl["itpm"]["rem"] | 0L;
    out.rl_otpm.lim = rl["otpm"]["lim"] | 0L;
    out.rl_otpm.rem = rl["otpm"]["rem"] | 0L;
  }

  Serial.printf("stats: in=%ld out=%ld usd=%.4f\n",
                out.in_tokens, out.out_tokens, (double)out.cost_usd);
  Serial.printf("  day: in=%ld out=%ld usd=%.4f\n",
                out.day.in_tokens, out.day.out_tokens, (double)out.day.cost_usd);
  Serial.printf("  week: in=%ld out=%ld usd=%.4f\n",
                out.week.in_tokens, out.week.out_tokens, (double)out.week.cost_usd);
  if (out.rl_valid)
    Serial.printf("  rl: rpm=%ld/%ld itpm=%ld/%ld otpm=%ld/%ld\n",
                  out.rl_rpm.rem, out.rl_rpm.lim,
                  out.rl_itpm.rem, out.rl_itpm.lim,
                  out.rl_otpm.rem, out.rl_otpm.lim);
  if (out.month.valid)
    Serial.printf("  month: usd=%.2f lim=%.2f proj=%.2f (day %d/%d)\n",
                  (double)out.month.usd, (double)out.month.lim,
                  (double)out.month.projected_eom_usd,
                  out.month.day_of_month, out.month.days_in_month);
  else
    Serial.println("  month: (absent — fallback to v2 layout)");
  return true;
}

// ── Render cycle ──────────────────────────────────────────────────────────────

static void doRender(bool fetchOk, StatsData& fresh) {
  stampTime(fresh.timestamp);
  fresh.today_dow   = currentDow();
  fresh.fetch_error = !fetchOk;

  if (fetchOk) {
    lastGood = fresh;
    hasData  = true;
  } else if (hasData) {
    // Keep last-good data; only refresh timestamp and error flag
    stampTime(lastGood.timestamp);
    lastGood.today_dow   = currentDow();
    lastGood.fetch_error = true;
  }
  // If !hasData and !fetchOk, lastGood is zero-initialised — renders a blank screen with "!"

  renderLayout(disp, lastGood);
}

// ── Entry points ──────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nClaude Credit Monitor");

  disp.begin();

  // Buttons: internal pull-ups, active-low. GPIO0 is the strapping/BOOT pin —
  // safe to read as input once we're past reset.
  pinMode(BTN_BOOT, INPUT_PULLUP);
  pinMode(BTN_USER, INPUT_PULLUP);

  showMessage("connecting...");

  if (!connectWiFi()) {
    showMessage("WiFi failed", "check config.h");
    return;   // stay on error screen; loop() will retry via reconnect logic
  }

  showMessage("syncing time...");
  initNTP();

  showMessage("fetching...");
  StatsData fresh;
  bool ok = fetchStats(fresh);
  doRender(ok, fresh);
}

void loop() {
  // Sleep in 100 ms ticks for POLL_INTERVAL_MS, reconnecting if WiFi drops and
  // polling the buttons each tick. A BOOT press cuts the wait short to refresh now.
  const unsigned long fetchAt = millis() + POLL_INTERVAL_MS;
  bool forceFetch = false;

  while (!forceFetch && (long)(millis() - fetchAt) < 0) {
    delay(100);

    // BOOT → force an immediate /stats fetch
    if (buttonPressed(BTN_BOOT, bootPrevHigh)) {
      Serial.println("BOOT button → forcing refresh");
      forceFetch = true;
    }

    // USER → cycle view (scaffolded no-op; just redraw current data for now)
    if (buttonPressed(BTN_USER, userPrevHigh)) {
      viewMode = (viewMode + 1) % 3;
      Serial.printf("USER button → viewMode=%d (view cycling not yet implemented)\n", viewMode);
      if (hasData) renderLayout(disp, lastGood);
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost — reconnecting");
      lastGood.fetch_error = true;
      stampTime(lastGood.timestamp);
      renderLayout(disp, lastGood);   // show "!" while reconnecting
      connectWiFi();
    }
  }

  StatsData fresh;
  bool ok = fetchStats(fresh);
  doRender(ok, fresh);
}
