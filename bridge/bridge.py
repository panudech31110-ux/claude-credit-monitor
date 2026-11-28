"""v4 laptop bridge: Claude Code usage (via ccusage) -> Worker /ingest-cc.

Reads local Claude Code JSONL through `ccusage claude ...` (Claude only; plain
`ccusage daily` also mixes in Codex and other agents), computes the current
5-hour block, today, and this Mon-Sun week, and scores each against the plan
limits in bridge/.env.

Step 3 of the v4 build: prints the payload only, nothing is sent.

Usage:  python bridge/bridge.py
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from datetime import date, datetime, time, timedelta, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

HERE = Path(__file__).resolve().parent
ENV_PATH = HERE / ".env"

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


def main() -> int:
    env = load_env(ENV_PATH)
    payload = build_payload(env)
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
