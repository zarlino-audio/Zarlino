import { useCallback, useEffect, useMemo, useState } from "react";
import { api, type UploadedFile } from "../api";

/**
 * A/B audio compare — lets the Founder listen to two versions of a plugin
 * render (A = dry, B = processed) and pick a side, all from the web console.
 *
 * The local factory's `local_render_ab` tool uploads pairs named
 *   <plugin>-AB-A-<label>.wav and <plugin>-AB-B-<label>.wav
 * so we auto-detect them from the uploaded-files list, build object URLs from
 * the stored base64, and render side-by-side players with a decision button.
 */

interface ABPair {
  plugin: string;
  label: string;
  a: UploadedFile;
  b: UploadedFile;
  at: string;
}

/** Cache object URLs by file id so re-renders don't refetch/re-blob. */
const urlCache = new Map<string, string>();

export async function audioUrlFor(f: UploadedFile): Promise<string> {
  const hit = urlCache.get(f.id);
  if (hit) return hit;
  const detail = await api.fileDetail(f.id);
  if (!detail.data_base64) throw new Error(`No audio data for ${f.name}`);
  const bin = atob(detail.data_base64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  const url = URL.createObjectURL(new Blob([bytes], { type: detail.mime || "audio/wav" }));
  urlCache.set(f.id, url);
  return url;
}

const fmtSize = (b: number) => (b >= 1024 * 1024 ? `${(b / 1024 / 1024).toFixed(1)} MB` : `${Math.max(1, Math.round(b / 1024))} KB`);
const fmtRel = (iso: string): string => {
  const d = Date.now() - Date.parse(iso);
  if (!Number.isFinite(d)) return "";
  if (d < 60_000) return "just now";
  if (d < 3_600_000) return `${Math.floor(d / 60_000)}m ago`;
  if (d < 86_400_000) return `${Math.floor(d / 3_600_000)}h ago`;
  return `${Math.floor(d / 86_400_000)}d ago`;
};

function detectPairs(files: UploadedFile[]): ABPair[] {
  const byKey = new Map<string, Partial<ABPair> & { at: string }>();
  const keyOf = (name: string): { plugin: string; label: string; side: "a" | "b" } | null => {
    const m = name.match(/^([A-Za-z0-9_-]+)-AB-([AB])-([^.]+)\./);
    if (!m) return null;
    return { plugin: m[1]!, label: m[3]!, side: m[2]!.toLowerCase() as "a" | "b" };
  };
  for (const f of files) {
    if (f.kind !== "audio") continue;
    const k = keyOf(f.name);
    if (!k) continue;
    const key = `${k.plugin}::${k.label}`;
    const cur = byKey.get(key) ?? { at: f.created_at };
    cur[k.side] = f;
    if (f.created_at > cur.at) cur.at = f.created_at;
    byKey.set(key, cur);
  }
  return [...byKey.entries()]
    .filter(([, v]) => v.a && v.b)
    .map(([key, v]) => {
      const [plugin, label] = key.split("::");
      return { plugin: plugin!, label: label!, a: v.a!, b: v.b!, at: v.at };
    })
    .sort((x, y) => (x.at < y.at ? 1 : -1));
}

export function ABCompareCard({
  send,
  busy,
}: {
  /** Send a decision message to the Executive (wired to the chat composer). */
  send: (text: string) => void;
  busy?: boolean;
}) {
  const [pairs, setPairs] = useState<ABPair[] | null>(null);
  const [urls, setUrls] = useState<Record<string, string>>({});
  const [chosen, setChosen] = useState<number>(0);
  const [decided, setDecided] = useState<"A" | "B" | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  const load = useCallback(async () => {
    setErr(null);
    try {
      const files = await api.files();
      setPairs(detectPairs(files));
      setChosen(0);
      setDecided(null);
    } catch (e) {
      setErr(e instanceof Error ? e.message : String(e));
      setPairs([]);
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const pair = pairs && pairs.length > 0 ? pairs[Math.min(chosen, pairs.length - 1)] : null;

  // Build object URLs for the chosen pair when it changes.
  useEffect(() => {
    if (!pair) return;
    setLoading(true);
    Promise.all([audioUrlFor(pair.a), audioUrlFor(pair.b)])
      .then(([ua, ub]) => setUrls({ [pair.a.id]: ua, [pair.b.id]: ub }))
      .catch((e) => setErr(e instanceof Error ? e.message : String(e)))
      .finally(() => setLoading(false));
  }, [pair?.a.id, pair?.b.id]);

  const pick = (side: "A" | "B") => {
    if (!pair || decided || busy) return;
    setDecided(side);
    send(`🎧 A/B decision for ${pair.plugin} (${pair.label}): I prefer ${side}. ${side === "A" ? "Go with the DRY version." : "Go with the PROCESSED version."}`);
  };

  const note = useMemo(() => {
    if (!pair) return "";
    const span = (Date.parse(pair.b.created_at) - Date.parse(pair.a.created_at)) / 1000;
    return `Rendered ${fmtRel(pair.at)} · A dry · B processed${Number.isFinite(span) && Math.abs(span) < 120 ? " · identical takes" : ""}`;
  }, [pair]);

  if (pairs === null) {
    return (
      <div className="ab-compare faint small">
        🎧 Loading A/B tests…
      </div>
    );
  }
  if (!pair) {
    return (
      <div className="ab-compare faint small">
        🎧 No A/B audio yet — ask the Executive to run <span className="mono">local_render_ab</span> for a plugin.
        <button className="btn small" onClick={() => void load()} title="Refresh">↻</button>
      </div>
    );
  }

  return (
    <div className="ab-compare">
      <div className="ab-head">
        <span className="ab-title">🎧 A/B Test · {pair.plugin}</span>
        <span className="faint small">{pair.label} · {fmtRel(pair.at)}</span>
        {pairs.length > 1 && (
          <select className="ab-pick" value={chosen} onChange={(e) => { setChosen(Number(e.target.value)); setDecided(null); }}>
            {pairs.map((p, i) => (
              <option key={i} value={i}>{p.plugin} · {p.label}</option>
            ))}
          </select>
        )}
        <span className="spacer" />
        <button className="btn small" onClick={() => void load()} title="Refresh A/B list">↻</button>
      </div>
      <p className="small muted mb">{note}</p>
      <div className="ab-cols">
        <div className={`ab-col ${decided === "A" ? "ab-winner" : ""}`}>
          <div className="ab-col-head">
            <b>A — Dry</b>
            <span className="faint small">{fmtSize(pair.a.size_bytes)}</span>
          </div>
          {loading || !urls[pair.a.id] ? (
            <div className="faint small ab-loading">loading audio…</div>
          ) : (
            <audio controls preload="none" src={urls[pair.a.id]} className="ab-audio" />
          )}
          <button className="btn ab-btn" disabled={!!decided || busy} onClick={() => pick("A")} title="Tell the Executive you prefer A">
            {decided === "A" ? "✓ A chosen" : "👍 I prefer A"}
          </button>
        </div>
        <div className={`ab-col ${decided === "B" ? "ab-winner" : ""}`}>
          <div className="ab-col-head">
            <b>B — Processed</b>
            <span className="faint small">{fmtSize(pair.b.size_bytes)}</span>
          </div>
          {loading || !urls[pair.b.id] ? (
            <div className="faint small ab-loading">loading audio…</div>
          ) : (
            <audio controls preload="none" src={urls[pair.b.id]} className="ab-audio" />
          )}
          <button className="btn ab-btn" disabled={!!decided || busy} onClick={() => pick("B")} title="Tell the Executive you prefer B">
            {decided === "B" ? "✓ B chosen" : "👍 I prefer B"}
          </button>
        </div>
      </div>
      {decided && <p className="small green">✓ Decision recorded — the Executive has been told: <b>{decided}</b>.</p>}
      {err && <p className="small error">⚠ {err}</p>}
    </div>
  );
}
