CREATE TABLE ratelimits (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  updated_at INTEGER NOT NULL,
  rpm_limit INTEGER, rpm_remaining INTEGER, rpm_reset INTEGER,
  itpm_limit INTEGER, itpm_remaining INTEGER, itpm_reset INTEGER,
  otpm_limit INTEGER, otpm_remaining INTEGER, otpm_reset INTEGER
);
INSERT OR IGNORE INTO ratelimits (id, updated_at) VALUES (1, 0);
