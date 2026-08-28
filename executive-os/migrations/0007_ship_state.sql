-- Zarlino Executive OS — migration v7: ship-state reconciliation.
-- Mirrors SHIP_STATE_SQL in src/db/schema.ts (applied by the migration runner).
-- ZScorch and ZTame are LIVE on zarlinoaudio.com (v1.0.0) — mark them SHIPPED,
-- set Gate 10 to SHIPPED, and record shipment history (guarded for fresh DBs).

-- product_status lacked updated_at (the Ship action records history) — add it.
ALTER TABLE product_status ADD COLUMN updated_at TEXT;

UPDATE products SET status = 'SHIPPED', version = '1.0.0', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE name IN ('ZScorch', 'ZTame');

UPDATE pipeline_gates SET status = 'SHIPPED',
       note = 'Shipped — live on zarlinoaudio.com (v1.0.0)',
       updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE plugin_id IN ('ZScorch', 'ZTame') AND gate_number = 10;

INSERT OR IGNORE INTO product_status (id, product_id, status, gate, note, recorded_at, source, created_at)
SELECT 'ps_ship_zscorch', id, 'SHIPPED', 'Gate 10', 'Shipped — live on zarlinoaudio.com (v1.0.0)', strftime('%Y-%m-%dT%H:%M:%fZ','now'), 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now')
  FROM products WHERE name = 'ZScorch' AND NOT EXISTS (SELECT 1 FROM product_status WHERE id = 'ps_ship_zscorch');

INSERT OR IGNORE INTO product_status (id, product_id, status, gate, note, recorded_at, source, created_at)
SELECT 'ps_ship_ztame', id, 'SHIPPED', 'Gate 10', 'Shipped — live on zarlinoaudio.com (v1.0.0)', strftime('%Y-%m-%dT%H:%M:%fZ','now'), 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now')
  FROM products WHERE name = 'ZTame' AND NOT EXISTS (SELECT 1 FROM product_status WHERE id = 'ps_ship_ztame');
