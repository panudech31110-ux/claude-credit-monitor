CREATE TABLE org_spend (
  id INTEGER PRIMARY KEY CHECK (id = 1),  -- singleton
  updated_at INTEGER NOT NULL,
  month_usd REAL NOT NULL DEFAULT 0,
  month_limit_usd REAL NOT NULL DEFAULT 0,
  credit_balance_usd REAL NOT NULL DEFAULT 0,
  month_start_ts INTEGER NOT NULL DEFAULT 0  -- unix seconds, UTC month start
);
INSERT OR IGNORE INTO org_spend (id, updated_at) VALUES (1, 0);
