-- Product metrics (migration v16): adds the missing updated_at column to the
-- metrics table and seeds the headline product counters the Founder tracks —
-- Downloads and Licensed seats. INSERT OR IGNORE by deterministic id so
-- Founder/Executive updates are never overwritten on redeploy.
ALTER TABLE metrics ADD COLUMN updated_at TEXT;
INSERT OR IGNORE INTO metrics (id, name, value, unit, period, source, confidence, recorded_at, created_at)
VALUES
 ('metric_downloads', 'downloads', 238, 'downloads', strftime('%Y-%m','now'), 'founder', 'FACT', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('metric_licensed', 'licensed', 83, 'licenses', strftime('%Y-%m','now'), 'founder', 'FACT', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'));
