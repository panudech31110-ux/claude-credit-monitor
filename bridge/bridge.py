"""v4 laptop bridge: Claude Code usage (via ccusage) -> Worker /ingest-cc.

Reads local Claude Code JSONL through `ccusage claude ...` (Claude only; plain
`ccusage daily` also mixes in Codex and other agents), computes the current
5-hour block, today, and this Mon-Sun week, and scores each against the plan
limits in bridge/.env.

POSTs the payload to WORKER_URL/ingest-cc (X-Ingest-Auth: INGEST_SECRET) and
appends the result to bridge/bridge.log (rotates at 10 MB). Uses no Anthropic
keys. Run every 5 minutes from Windows Task Scheduler via pythonw.exe.

Usage:  python bridge/bridge.py            # build + send
        python bridge/bridge.py --dry-run  # build + print, send nothing
"""

from __future__ import annotations

import argparse
import json
import logging
import shutil
import subprocess
import sys
from datetime import date, datetime, time, timedelta
from logging.handlers import RotatingFileHandler
from pathlib import Path
from zoneinfo import ZoneInfo

import requests

HERE = Path(__file__).resolve().parent
ENV_PATH = HERE / ".env"
LOG_PATH = HERE / "bridge.log"
LOG_MAX_BYTES = 10 * 1024 * 1024
HTTP_TIMEOUT_S = 20

log = logging.getLogger("bridge")

# Keeps a console window from flashing up when run via pythonw.exe.
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def load_env(path: Path) -> dict[str, str]:
    """Minimal KEY=VALUE parser; strips inline '# comments' and quotes."""
    env: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or "=" not in line:
            continue
        key, val = line.split("=", 1)
        env[key.strip()] = val.strip().strip('"').strip("'")
    return env


def run_ccusage(*args: str) -> dict:
    exe = shutil.which("ccusage")
    if not exe:
        raise RuntimeError("ccusage not found on PATH (npm install -g ccusage)")
    proc = subprocess.run(
        [exe, "claude", *args, "--json"],
        capture_output=True, text=True, encoding="utf-8",
        timeout=120, creationflags=NO_WINDOW,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"ccusage {' '.join(args)} failed: {proc.stderr.strip()[:500]}")
    return json.loads(proc.stdout)


def window(cost: float, tokens: int, limit: float, reset_at: int) -> dict:
    return {
        "cost": round(cost, 4),
        "pct": round(cost / limit, 4) if limit > 0 else 0.0,
        "reset_at": int(reset_at),
        "tokens": int(tokens),
    }


def session_window(limit: float) -> dict:
    """Active 5-hour block; zeros with reset_at=0 when no block is running."""
    blocks = run_ccusage("blocks", "--active").get("blocks", [])
    active = next((b for b in blocks if b.get("isActive")), None)
    if not active:
        return window(0.0, 0, limit, 0)
    end = datetime.fromisoformat(active["endTime"].replace("Z", "+00:00"))
    return window(active.get("costUSD", 0.0), active.get("totalTokens", 0), limit, end.timestamp())


def day_and_week_windows(tz: ZoneInfo, day_limit: float, week_limit: float) -> tuple[dict, dict]:
    """One `daily` call from Monday to today covers both windows."""
    now = datetime.now(tz)
    today = now.date()
    monday = today - timedelta(days=today.weekday())

    rows = run_ccusage(
        "daily", "--since", monday.strftime("%Y%m%d"), "--until", today.strftime("%Y%m%d"),
        "--timezone", str(tz),
    ).get("daily", [])

    week_cost = sum(r.get("totalCost", 0.0) for r in rows)
    week_tok = sum(r.get("totalTokens", 0) for r in rows)
    today_row = next((r for r in rows if r.get("date") == today.isoformat()), {})

    def midnight(d: date) -> int:
        return int(datetime.combine(d, time.min, tzinfo=tz).timestamp())

    today_w = window(today_row.get("totalCost", 0.0), today_row.get("totalTokens", 0),
                     day_limit, midnight(today + timedelta(days=1)))
    week_w = window(week_cost, week_tok, week_limit, midnight(monday + timedelta(days=7)))
    return today_w, week_w


def build_payload(env: dict[str, str]) -> dict:
    tz = ZoneInfo(env.get("TIMEZONE", "Asia/Bangkok"))
    today_w, week_w = day_and_week_windows(
        tz, float(env["PLAN_DAILY_LIMIT_USD"]), float(env["PLAN_WEEKLY_LIMIT_USD"]))
    return {
        "session": session_window(float(env["PLAN_SESSION_LIMIT_USD"])),
        "today": today_w,
        "week": week_w,
    }


def send(env: dict[str, str], payload: dict) -> None:
    url = env["WORKER_URL"].rstrip("/") + "/ingest-cc"
    resp = requests.post(
        url, json=payload, timeout=HTTP_TIMEOUT_S,
        headers={"X-Ingest-Auth": env["INGEST_SECRET"]},
    )
    if resp.status_code != 200:
        raise RuntimeError(f"POST {url} -> {resp.status_code}: {resp.text[:300]}")


def setup_logging() -> None:
    log.setLevel(logging.INFO)
    fh = RotatingFileHandler(LOG_PATH, maxBytes=LOG_MAX_BYTES, backupCount=2, encoding="utf-8")
    fh.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
    log.addHandler(fh)
    # pythonw.exe has no console (sys.stderr is None); only echo when there is one.
    if sys.stderr is not None:
        log.addHandler(logging.StreamHandler(sys.stderr))


def summary(p: dict) -> str:
    return "  ".join(
        f"{k} ${p[k]['cost']:.2f} ({p[k]['pct'] * 100:.1f}%)" for k in ("session", "today", "week"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true", help="print the payload, don't send")
    args = ap.parse_args()

    if args.dry_run:
        print(json.dumps(build_payload(load_env(ENV_PATH)), indent=2))
        return 0

    setup_logging()
    try:
        env = load_env(ENV_PATH)
        payload = build_payload(env)
        send(env, payload)
    except Exception as e:  # one line per failed cycle; the next run retries
        log.error("failed: %s", e)
        return 1
    log.info("sent  %s", summary(payload))
    return 0


if __name__ == "__main__":
    sys.exit(main())
