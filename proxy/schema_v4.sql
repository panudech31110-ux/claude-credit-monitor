-- v4: Claude Code usage pushed from the laptop bridge (ccusage) via POST /ingest-cc.
-- Singleton row; updated_at = 0 means "never ingested".
CREATE TABLE IF NOT EXISTS cc_usage (
  id INTEGER PRIMARY KEY CHECK (id = 1),  -- singleton
  updated_at INTEGER NOT NULL,            -- unix seconds of last ingest
  session_cost REAL, session_pct REAL, session_reset_at INTEGER, session_tokens INTEGER,
  today_cost REAL,   today_pct REAL,   today_reset_at INTEGER,   today_tokens INTEGER,
  week_cost REAL,    week_pct REAL,    week_reset_at INTEGER,    week_tokens INTEGER
);
INSERT OR IGNORE INTO cc_usage (id, updated_at) VALUES (1, 0);
